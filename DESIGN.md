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
| Target | ESP32-P4 **verified**; other ESP-DL targets allowed but experimental | — |

`person_detect` is SoC-agnostic and only **warns** on non-P4 targets
(`__init__.py` → `_final_validate`), since ESP-DL runs on the S3 too. The
`esp_video_camera` backend, by contrast, hard-fails off-P4 — it needs the P4's
MIPI-CSI + ISP + PPA silicon (see §2, and the README *Portability* section for
the S3 route via `camera_id`).

## 2. Frame acquisition — as built

### Decision: a dedicated ESP32-P4 MIPI-CSI backend (`esp_video_camera`) behind a `FrameSource` seam. The "ESPHome camera by ID" path is the intended-portable fallback, but is not usable today.

`person_detect` consumes typed frames through one abstraction,
`frame_source.h::FrameSource` (`init/start/stop/acquire/release`, yielding a
`FrameView` of typed RGB). Two backends implement it:

- **`esp_video_camera` (raw, this repo) — the working, verified path.** Drives
  the P4's MIPI-CSI sensor through Espressif's `esp_video` (V4L2) +
  `esp_cam_sensor` + ISP stack, then the PPA (rotate + RAW→RGB888). This is what
  the D1001 uses and what has been hardware-verified.
- **`EsphomeCameraSource` (`camera_id`) — the intended portable path, currently
  unusable.** Designed to consume an ESPHome `camera` and decode its JPEG
  frames. It compiles and is wired behind the same seam, but there is nothing to
  point it at on the P4 (see below).

The two are mutually exclusive (`frame_source_id` vs `camera_id`); the detector
does not change between them.

### Why a custom camera backend (and not `esp32_camera` or the `camera` framework)

The obvious question is why we didn't just reuse ESPHome's existing camera
components. Two of them exist, and neither can drive a P4 MIPI-CSI sensor:

- **`esp32_camera`** is a **DVP (parallel) camera** driver — its required config
  is `data_pins:` (an 8-bit parallel bus), `vsync_pin`, `href_pin`,
  `pixel_clock_pin`, and an ESP-driven `external_clock` (the classic
  OV2640/OV3660 / Ai-Thinker wiring, via the legacy `esp32-camera` library on
  ESP32 / ESP32-S3). **MIPI-CSI has none of those signals** — it's a high-speed
  serial differential interface — so `esp32_camera` physically cannot talk to the
  D1001's SC2356. (It *is* the right component for an **S3 + OV2640**, which is
  the documented S3 route — see the README *Portability* section.)
- **The modular `camera` framework** (`esphome/components/camera/`) is the
  abstraction we designed `camera_id` against — `esp32_camera` itself is one
  implementation of it (its `on_image` yields `esp32_camera::CameraImageData`).
  But as of ESPHome 2026.6.x it ships a C++ base with **no user-configurable
  MIPI-CSI camera platform** — there is no CSI `camera` to instantiate and hand
  to `camera_id`. So the portable path we built has no counterpart to consume.

That left Espressif's own MIPI-CSI stack (`esp_video` + `esp_cam_sensor` + ISP +
PPA) as the only way to actually get frames off the P4's camera.
`esp_video_camera` is therefore a **thin ESPHome wrapper** over that official
stack — codegen, the V4L2 capture loop, exposure/gain, rotation — not a
from-scratch sensor driver. When upstream adds a real CSI `camera` platform, the
right move is to retire most of this: re-expose it *as* a `camera` platform, or
just consume the upstream one via `camera_id`. The `FrameSource` seam is what
makes that swap free — `person_detect` wouldn't change.

### Component structure (the camera is already its own component)

`esp_video_camera` is a **separate ESPHome component** from `person_detect` — the
camera concern is already factored out; `person_detect` only knows the
`FrameSource` interface. The one remaining wart is dependency *direction*:
`FrameSource` lives in `components/person_detect/frame_source.h`, so
`esp_video_camera` `DEPENDENCIES: [person_detect]` to obtain it — i.e. the camera
depends on the detector, which is backwards. The clean finish is to move the
`FrameSource` contract into a neutral shared spot (its own tiny component/header)
so neither depends on the other, or to fold `esp_video_camera` into ESPHome's
`camera` platform once CSI lands upstream. Not yet done; noted here so it isn't
forgotten.

### Frame format handling (`camera_id` path)

The shared `CameraImage` buffer ESPHome hands listeners is JPEG-encoded (the same
buffer streamed to the HA API / web clients). ESP-DL ships a JPEG decoder
(`dl::image::sw_decode_jpeg`, plus the P4 hardware JPEG codec), so
`EsphomeCameraSource` decodes JPEG → RGB888 before inference; a raw
`PixelFormat` would take a zero-copy fast path. Both are implemented but
unverified (no CSI camera to feed them). The raw `esp_video_camera` path skips
all of this: the ISP/PPA hand it RGB888 directly.

## 3. Inference

* **Model:** `espressif/pedestrian_detect` (ESP Component Registry, v0.3.0),
  built on ESP-DL. Class `PedestrianDetect`; `detect->run(img_t)` returns
  `std::list<dl::detect::result_t>`. Input **224×224×3**, INT8-quantized,
  **~75 ms/inference measured on ESP32-P4** (D1001, 1280×720 capture). Pulled via the IDF component
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
* **Privacy toggle:** the switch is a detection on/off control (default on).
  When turned off, the task stops the frame source (`FrameSource::stop()`, which
  fully idles the camera) and blocks on a notify — no capture, no inference.

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

1. ~~Raw vs. JPEG frames.~~ **Resolved.** The raw `esp_video_camera` backend is
   implemented and hardware-verified; it's the working path, not a follow-up.
   The `camera_id`/JPEG path remains unverified only because ESPHome ships no CSI
   `camera` to feed it (§2).
2. **`camera_id` path unverified.** `EsphomeCameraSource` (JPEG decode + listener
   sampling) compiles but has never run against a real ESPHome camera — there's
   no configurable CSI platform on the P4, and it's the future S3/DVP route. Its
   request/stream semantics and channel handling need verifying when first used
   on hardware.
3. **Sensor exposure/rotation are board-tuned.** Exposure/gain defaults and the
   `rotation: auto` IMU axis→orientation mapping were calibrated on the D1001;
   other boards/sensors may need different values (both are config options, and
   the IMU mapping is logged for recalibration).
4. **Exact flash/PSRAM figures** — the README carries measured D1001 numbers;
   they shift with model build and capture resolution, so re-measure per build.

## 8. Non-goals (hard constraints)

No off-board inference, no cloud, no streaming-to-HA fallback for detection, no
face recognition / identity / biometric persistence. Person presence only.
