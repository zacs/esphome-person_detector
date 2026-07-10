# esphome-person_detector

**On-device human-presence detection for ESPHome on ESP32-P4.**

`person_detect` is an ESPHome external component that uses a camera to decide
whether a **person is in frame** — presence only, *not* identity — entirely
on-device, and exposes it to Home Assistant as an occupancy `binary_sensor`.

The **detection model**, the **camera sensor**, and the **frame-source backend**
are all pluggable (see [Extending](#extending)). The **Seeed Studio reTerminal
D1001** (ESP32-P4 + MIPI-CSI SC2356) is the **first supported board**, not the
point of the project.

## Privacy — what leaves the device: nothing

- **No image, frame, embedding, or audio ever leaves the device.** No cloud API,
  no companion server, no streaming to HA. All inference is on-device.
- **No identity, no recognition.** No face-recognition models, no biometric data
  stored or persisted. It answers exactly one question: *is a person in frame
  right now?*
- The privacy `switch` (optional) releases the camera and stops all work when off.

## Requirements

`person_detect` fails codegen with a clear message if these aren't met:

| | |
|---|---|
| SoC | **ESP32-P4 only** (codegen hard-fails on any other target) |
| Framework | **ESP-IDF only** (the P4 has no Arduino core) |
| PSRAM | **Required** — the model runtime and frame buffers live here |
| Flash | **`flash_size: 16MB`** — the embedded model makes the app image ~2.3 MB; 16 MB lets ESPHome auto-size an app partition that fits (no custom partition CSV) |

Tested against **ESPHome 2026.6.0** / **ESP-IDF v5.5.4** with the
`espressif/pedestrian_detect` v0.3.0 model (ESP-DL, 224×224×3 INT8).

## Quick start

### Option A — flash the prebuilt reTerminal D1001 image (fastest)

Grab `reterminal-presence.factory.bin` from the [latest
release](https://github.com/zacs/esphome-person_detector/releases), flash it to
offset `0x0` (the [ESPHome web flasher](https://web.esphome.io) or
`esptool.py --chip esp32p4 write_flash 0x0 …`), then join the **"reTerminal
Presence"** Wi-Fi AP / captive portal (or Improv over USB) to set your network.

### Option B — start from the example config

[`example/reterminal_d1001.yaml`](example/reterminal_d1001.yaml) is a complete,
compilable D1001 config (board bring-up + camera wiring + detector). Copy it,
drop in your Wi-Fi, and build:

```bash
pip install "esphome==2026.6.0"
esphome run example/reterminal_d1001.yaml
```

### Option C — add it to an existing ESP32-P4 config

Pull the component in, point the detector at a camera backend, and expose a
sensor. On the D1001 the camera backend is the built-in **`esp_video_camera`**
(CSI + ISP → RGB888); its power/reset lines sit on an XL9535 I²C expander, so
copy the `i2c:` + `xl9535:` + `esp_video_camera:` blocks from the example for
the full wiring. The detector itself is just:

```yaml
external_components:
  - source: github://zacs/esphome-person_detector@v0.1.12   # pin a tag
    components: [person_detect, esp_video_camera]

person_detect:
  id: presence
  frame_source_id: d1001_cam   # id of your esp_video_camera (see below)

binary_sensor:
  - platform: person_detect
    person_detect_id: presence
    name: "Room Occupied"       # device_class defaults to "occupancy"
```

> **`frame_source_id` vs `camera_id`:** use **`frame_source_id`** to point at an
> `esp_video_camera` (the raw MIPI-CSI path — this is what the D1001 uses). Only
> use `camera_id` if you're feeding a stock ESPHome *JPEG* `camera:` instead.
> Set exactly one. See [Frame sources](#frame-sources).

## Configuration

### `esp_video_camera:` — the ESP32-P4 MIPI-CSI backend

Owns the sensor and hands the detector upright RGB888 frames.

```yaml
esp_video_camera:
  id: d1001_cam
  sda: 37                # sensor SCCB (I²C) pins — D1001: 37/38
  scl: 38
  i2c_port: 1            # SCCB I²C controller; MUST differ from your `i2c:` bus
  resolution: 1280x720
  rotation: auto         # IMU-picked at boot; or pin 0 / 90 / 180 / 270
  imu:                   # only needed for rotation: auto
    i2c_id: bus_expander
    address: 0x6A        # D1001 LSM6DS3TR
  # exposure: 811        # raw sensor units; omit for a bright auto default
  # gain: 63             # raw sensor units; omit for auto
  # power/reset lines (D1001 wires these to the XL9535 expander):
  enable_pin: { xl9535: cam_expander, number: 1 }
  powerdown_pin: { xl9535: cam_expander, number: 3 }
  reset_pin: { xl9535: cam_expander, number: 11 }
```

- **`rotation: auto`** reads the accelerometer once at boot and orients so a
  standing person is upright — handy for a device you reposition. It needs the
  device roughly upright (wall/desk); lying flat it holds the last orientation.
  An explicit `rotation:` always wins and is best for a fixed mount.
- **`exposure` / `gain`** default to a bright value because the SC2356 powers up
  near-black. If a room is too dark/bright, nudge them (raw sensor units, logged
  at boot with their valid range).

### `person_detect:` — the detector

```yaml
person_detect:
  id: presence
  frame_source_id: d1001_cam  # an esp_video_camera; OR camera_id: for a JPEG camera
  interval: 1500ms            # inference cadence (default 1500ms, min 100ms)
  confidence_threshold: 60%   # min score to count a detection (default 60%)
  clear_after: 3              # consecutive misses before clearing (default 3)
  model: pedestrian           # selectable model (default: pedestrian)
  # advanced task tuning (defaults shown):
  task_priority: 2            # keep below a UI/LVGL task
  task_stack_size: 8192
  task_core: 1
  on_person_detected:
    - logger.log: "Person detected"
  on_person_cleared:
    - logger.log: "Room empty"
```

### `binary_sensor` / `sensor` / `switch`

```yaml
binary_sensor:
  - platform: person_detect
    person_detect_id: presence
    name: "Room Occupied"
    filters:
      - delayed_off: 15s        # optional hold-open on top of clear_after

sensor:                          # optional
  - platform: person_detect
    person_detect_id: presence
    type: confidence             # best-box score, 0–100 %
    name: "Presence Confidence"
  - platform: person_detect
    person_detect_id: presence
    type: count                  # boxes over threshold
    name: "People Count"

switch:                          # optional privacy toggle
  - platform: person_detect
    person_detect_id: presence
    name: "Presence Detection"   # off => camera idle, no inference
```

## Frame sources

`person_detect` takes frames from exactly one backend:

| Key | Backend | Frames | Use when |
|---|---|---|---|
| `frame_source_id` | **`esp_video_camera`** (this repo) | raw RGB888 (CSI + ISP + PPA) | ESP32-P4 MIPI-CSI sensors (SC2356 / D1001) — **the path the D1001 uses** |
| `camera_id` | a stock ESPHome `camera:` | JPEG, decoded on-device | a board that already exposes a JPEG camera to ESPHome |

Both sit behind one `FrameSource` seam ([`DESIGN.md`](DESIGN.md) §2). The
`camera_id` path exists for composability, but ESPHome (as of 2026.6.0) ships no
user-configurable MIPI-CSI camera platform, so on the P4 you use
`esp_video_camera`.

## Coexisting with an LVGL / display config

Inference runs on its own low-priority, pinned FreeRTOS task (`task_priority`,
`task_core`), publishes only from the main loop, and keeps every large buffer in
PSRAM — so it stays out of the way of an 800×1280 MIPI-DSI LVGL UI. If your UI
task is pinned to a core, pin detection to the other with `task_core`.

## Tuning

- **`confidence_threshold`** — raise (70–80 %) to cut false positives from
  clutter; lower (45–55 %) if real people are missed.
- **`interval`** — presence doesn't need video framerates; 1–2 s is a good
  default. Inference is ~75 ms on the P4, so the task idles most of the interval.
- **`clear_after` + `delayed_off`** — `clear_after` debounces single-frame
  misses; a `delayed_off:` filter adds occupancy-style hold-open.
- **Exposure / lighting** — the SC2356 is a small 2 MP sensor. If confidence sags
  in dim rooms, raise `exposure:`/`gain:`; avoid strong backlight (windows behind
  the subject) which silhouettes people.

## Diagnostics

`logger: level: DEBUG` prints, per inference: time, frame dims, box count, best
score, a min/max/mean frame-brightness probe, and free PSRAM. `dump_config`
prints the model, cadence, threshold, task placement, PSRAM cost/low-water, last
inference time, and capture-failure count. On boot `esp_video_camera` logs the
SCCB port, applied exposure/gain (with ranges), and — for `rotation: auto` — the
raw accelerometer reading and chosen rotation.

## Memory / flash budget

All large buffers are in PSRAM; internal SRAM use is limited to the inference
task stack. **Measured static footprint** (CI `esphome compile`, ESP32-P4,
ESPHome 2026.6.0 / ESP-IDF 5.5.4, `esp_video_camera` + `person_detect` + ESP-DL +
pedestrian model):

| Metric | Value |
|---|---|
| Total image | **~2.29 MB — Flash 13.6 %** of 16 MB |
| `.rodata` (model weights + const) | 596 KB |
| Internal RAM (static) | ~5.8 % of 512 KB |
| PPA / capture / tensor-arena buffers | **PSRAM, allocated at runtime** (not in the image) |

Runtime PSRAM depends on resolution/rotation — read it on your build from the
`model runtime PSRAM cost` (startup) and `PSRAM free low-water` (`dump_config`)
log lines. Rough placement: model weights → flash; ESP-DL tensor arena, the
RGB888 frame (1280×720 ≈ 2.6 MB), and camera buffers → PSRAM; task stack →
internal SRAM.

## How it works

`PersonDetector` pulls one typed RGB frame per `interval` from a `FrameSource`
(on a dedicated low-priority FreeRTOS task), runs an ESP-DL detection model
(resized to the model input via the P4's hardware image path), and marshals the
result to the main loop for debouncing, state publishing, and triggers. See
[`DESIGN.md`](DESIGN.md) for the full design and
[`BRINGUP.md`](BRINGUP.md) for a first-flash checklist.

## Extending

The component is layered so contributors can add support without touching the core:

- **A detection model** — a row in the `MODELS` table + a `case` in
  `PersonDetector::create_model_()` (`components/person_detect/`); any ESP-DL
  detector returning `dl::detect::result_t` boxes fits.
- **A camera sensor** — a `sensor:` enum entry + its `esp_cam_sensor` Kconfig in
  `components/esp_video_camera/__init__.py`; the capture/PPA path is
  sensor-independent.
- **A frame-source backend** — implement `person_detect::FrameSource`
  (`frame_source.h`) and yield `FrameView`s; the detector consumes them unchanged.
- **A board** — a device YAML (its `esp32:` details, camera wiring, and a
  `person_detect` block). The D1001 files under `example/` and `firmware/` are
  the first; others sit alongside them.

Contributions for new models, sensors, and boards are welcome.

## License

C++ under **GPL-3.0-only** to match the ESPHome ecosystem (see `LICENSE`).
Bundled Espressif ESP-DL and the pedestrian model are Apache-2.0 / MIT — see
`NOTICE`.
