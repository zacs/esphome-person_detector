# DESIGN — `person_detect` ESPHome external component

On-device human *presence* detection (not identity, not recognition) for
ESP32-P4 camera boards, exposed to Home Assistant as a `binary_sensor`.
First target: **Seeed Studio reTerminal D1001** (ESP32-P4 + MIPI-CSI SC2356).

All claims below were verified against upstream source in July 2026; the exact
files are cited inline so they can be re-checked as these fast-moving APIs
evolve. Where a fact could **not** be fully verified without a P4 toolchain in
hand, it is called out explicitly under *Open risks*.

---

## 1. Toolchain / version pins

| Thing | Value | Source |
|---|---|---|
| ESPHome release targeted | **2026.6.0** (latest, June 2026) | esphome.io/changelog |
| ESP-IDF pinned by that release | **v5.5.4** | esphome `dev` `platformio.ini`: `pioarduino/framework-espidf@…/v5.5.4/…` (pioarduino platform `55.03.39`) |
| Framework | ESP-IDF only (P4 has no Arduino core) | — |
| Target | ESP32-P4 only for v1 (`CONFIG_IDF_TARGET_ESP32P4`) | — |

Codegen hard-fails on any non-P4 target (see `__init__.py` → `_validate_target`).

## 2. Frame acquisition — chosen path

### Decision: consume frames from an ESPHome **camera source by ID** (primary), behind a `FrameSource` interface.

ESPHome merged a *modular camera framework* with MIPI-CSI support
(`esphome/components/camera/`, PR "Modular Camera Framework with MIPI-CSI
support", commit `f1e5683`). The relevant public API
(`esphome/components/camera/camera.h`, `dev` branch) is:

```
class Camera {
  static Camera *instance();
  void add_listener(CameraListener *);
  void request_image(CameraRequester);
  void start_stream(CameraRequester);
  void stop_stream(CameraRequester);
};
class CameraListener {
  virtual void on_camera_image(const std::shared_ptr<CameraImage> &);
  virtual void on_stream_start();
  virtual void on_stream_stop();
};
class CameraImage { uint8_t *get_data_buffer(); size_t get_data_length(); ... };
enum PixelFormat { PIXEL_FORMAT_GRAYSCALE, PIXEL_FORMAT_RGB565, PIXEL_FORMAT_BGR888 };
enum CameraRequester { IDLE, API_REQUESTER, WEB_REQUESTER };
```

Our component **owns no camera hardware**. It registers a `CameraListener` on
the camera referenced by `camera_id`, samples one frame per `interval`, and runs
inference. This is the architecture the brief asks for: it *composes* with the
API/web camera consumers instead of monopolising the sensor. The board's sensor
pins, I2C address, CSI lanes, etc. live in the **separate** ESPHome camera
platform config, not here — so this component stays board-agnostic.

The frame path is isolated behind `frame_source.h::FrameSource`. A second
backend that drives `esp_video` / `esp_cam_ctlr` + ISP directly (owning the
sensor) can be dropped in later for boards whose sensor ESPHome does not yet
support — see *Open risks* #1.

### Frame format handling
`CameraImage::get_data_buffer()` on the shared image handed to listeners is
documented in `camera.h` as **JPEG-encoded** (it is the same buffer streamed to
the HA API / web clients). ESP-DL ships a JPEG decoder
(`dl::image::sw_decode_jpeg`, plus the P4 hardware JPEG codec), so
`EsphomeCameraSource` decodes JPEG → RGB888 before inference. If a future
camera build instead hands listeners a *raw* `PixelFormat`, we take a zero-copy
fast path (`PIXEL_FORMAT_RGB565` → `DL_IMAGE_PIX_TYPE_RGB565LE`,
`PIXEL_FORMAT_BGR888` → `RGB888` with channel handling, `GRAYSCALE` → `GRAY`).
Both paths are implemented; the format is chosen at runtime from the frame's
`CameraImageSpec`.

## 3. Inference

* **Model:** `espressif/pedestrian_detect` (ESP Component Registry, v0.3.0),
  built on ESP-DL. Class `PedestrianDetect`; `detect->run(img_t)` returns
  `std::list<dl::detect::result_t>`. Input **224×224×3**, INT8-quantized,
  reported ~**55 ms/inference on ESP32-P4**. Pulled via the IDF component
  manager (`espressif/pedestrian_detect`), which brings `esp-dl` transitively.
* **Types** (`esp-dl/.../dl_image_define.hpp`, `dl_detect_define.hpp`):
  `img_t { void *data; uint16_t width; uint16_t height; pix_type_t pix_type; }`;
  `result_t { int category; float score; std::vector<int> box; std::vector<int> keypoint; ... }`
  where `box = {x1, y1, x2, y2}`.
* **Scaling:** ESP-DL's detect pipeline resizes the input frame to the model
  resolution internally; on ESP32-P4 that resize / color-convert is
  hardware-accelerated (PPA / 2D-DMA) inside `dl::image`. We therefore hand the
  decoded full frame to `run()` rather than hand-rolling PPA calls, and log
  per-stage timing so the acceleration can be verified on-device.
* **Selectable model:** `model:` key is a schema enum with one value today
  (`pedestrian`); adding another registry model is a one-line table entry.

## 4. Runtime model

* Inference runs in a **dedicated FreeRTOS task**, created in `setup()`,
  configurable priority/stack/core (defaults: priority `2` — below ESPHome's
  main loop and well below LVGL/UI — stack `8 KiB`, core `1`). It never blocks
  the main loop.
* The task loop: acquire a frame → run inference → publish result to a
  guarded slot → `vTaskDelay(interval)`. Results are marshalled back to the main
  loop in `loop()`, which is the only place ESPHome entity state is published
  (state publishing is not task-safe).
* **Privacy toggle:** when disabled, the task stops the camera stream
  (`stop_stream`) and idles on a notify — the sensor does no work.

## 5. Output semantics

* `binary_sensor` (device_class `occupancy`): person present. Component-level
  `clear_after` = N consecutive negative inferences before clearing (debounce),
  on top of any standard ESPHome `filters:` (`delayed_off`) the user adds.
* `sensor` (optional): `type: confidence` (0–100, best-box score) and/or
  `type: count` (number of boxes ≥ threshold).
* Triggers: `on_person_detected`, `on_person_cleared`.
* `switch` (optional): runtime enable/disable (privacy).

## 6. Memory / flash budget (documented, to be confirmed on-device)

| Item | Placement | Approx. size |
|---|---|---|
| Pedestrian model weights | Flash (rodata, `.espdl`) | ~1–2 MB (measure at link; see README) |
| ESP-DL tensor arena / runtime | **PSRAM** | model-dependent, hundreds of KiB |
| Camera frame buffer(s) | **PSRAM** (owned by camera component) | 2 × frame |
| Decoded RGB888 working buffer | **PSRAM** | W×H×3 (e.g. 1280×720×3 ≈ 2.6 MB) |
| Task stack | Internal SRAM | 8 KiB (configurable) |
| IRAM | — | none added beyond ESP-DL's own |

Coexists with the 800×1280 LVGL/MIPI-DSI config by keeping every large buffer in
the shared 32 MB PSRAM and internal SRAM use to the single task stack. The
README carries a "measure it yourself" section because exact figures depend on
the chosen model build and are not asserted here without a link map.

## 7. Open risks (honest list)

1. **Raw vs. JPEG frames to a listener.** `camera.h` describes the shared
   `CameraImage` buffer as JPEG. If per-interval JPEG decode of a 720p frame
   proves too costly, or if a board's sensor is unsupported by ESPHome's camera
   framework, the `FrameSource` interface lets us swap in a direct
   `esp_video` backend (own the SC2356 via `esp_cam_sensor` + `esp_cam_ctlr`,
   ISP → RGB565, PPA → 224×224). That backend is *specified* here and left as a
   follow-up implementation; the interface seam is in place.
2. **Camera request/stream semantics.** `CameraRequester` exposes only
   `IDLE/API/WEB`. It is not yet confirmed that a non-API consumer can pull a
   single frame via `request_image`. `EsphomeCameraSource` uses `start_stream`
   + listener sampling as the robust default and documents the `request_image`
   alternative; revisit once the framework stabilises.
3. **Model channel order.** `PIXEL_FORMAT_BGR888` vs. the model's expected RGB
   ordering — handled by feeding `RGB565`/decoded `RGB888` and noting the swap;
   verify detection quality on-device.
4. **Exact flash/PSRAM figures** — placeholders until measured against a real
   link map (README documents how).

## 8. Non-goals (hard constraints)

No off-board inference, no cloud, no streaming-to-HA fallback for detection, no
face recognition / identity / biometric persistence. Person presence only.
