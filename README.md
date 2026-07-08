# esphome-person_detector

**On-device human presence detection for ESPHome on ESP32-P4.**

`person_detect` is a general-purpose ESPHome external component that uses a
camera to detect whether a **person is in frame** — presence only, *not*
identity — entirely on-device, and exposes it to Home Assistant as a
`binary_sensor`. Consumed via ESPHome's `external_components:` mechanism.

It's built to be extended: the **detection model**, the **camera sensor**, and
the **frame-source backend** are all pluggable (see [Extending](#extending)).
The **Seeed Studio reTerminal D1001** (ESP32-P4 + MIPI-CSI SC2356) is simply the
**first supported board** — not the point of the project.

---

## What it does / does not do

**Does**

- Runs a small quantized (INT8) pedestrian/human **detection** model (Espressif
  ESP-DL) on the ESP32-P4, on a low-priority FreeRTOS task.
- Consumes frames from an **ESPHome camera source by ID**, so it composes with
  other camera consumers instead of owning the sensor.
- Publishes a debounced occupancy `binary_sensor`, optional confidence / count
  `sensor`s, `on_person_detected` / `on_person_cleared` triggers, and a runtime
  privacy `switch`.

**Does NOT — privacy statement**

- **No image, frame, embedding, or audio ever leaves the device.** There is no
  cloud API, no companion server, and no streaming-to-HA fallback for detection.
  All inference is on-device.
- **No identity, no recognition.** It does not use face-recognition models and
  never stores or persists any biometric data. It answers exactly one question:
  *"is a person in frame right now?"*
- When the privacy `switch` is off, the camera stream is released and the sensor
  does no work at all.

## Supported hardware

| | |
|---|---|
| SoC | **ESP32-P4 only** (v1). Codegen hard-fails on any other target. |
| Framework | **ESP-IDF only** (the P4 has no Arduino core). |
| Reference board | Seeed Studio reTerminal D1001 (ESP32-P4 + MIPI-CSI **SC2356**, 2 MP) |
| PSRAM | Required (32 MB on the D1001). Model runtime + frame buffers live here. |

The detector itself is board-agnostic — it only consumes typed frames from a
`FrameSource`. Any ESP32-P4 MIPI-CSI board works by pointing `esp_video_camera`
at its sensor/pins (or by feeding an ESPHome JPEG camera); see
[Extending](#extending) to add a sensor or board.

## Toolchain / versions tested

| Thing | Value |
|---|---|
| ESPHome | **2026.6.0** (latest at time of writing) |
| ESP-IDF | **v5.5.4** (the version ESPHome 2026.6.0 pins) |
| Model | `espressif/pedestrian_detect` v0.3.0 (ESP-DL), input 224×224×3 INT8 |

Reproducible build:

```bash
pip install "esphome==2026.6.0"
esphome compile example/reterminal_d1001.yaml
```

(CI runs both `esphome config` and `esphome compile` on that example — see
`.github/workflows/ci.yml`.)

## Using it as an add-on component

This is a standalone ESPHome **external component** — you don't fork it or copy
files in. You point an existing ESP32-P4 ESPHome config at this repo and ESPHome
pulls it in at build time. It's designed to bolt onto a device that's already
doing something else (e.g. an LVGL display): it owns no camera hardware and adds
nothing to the main loop.

### 1. Pull the component in

```yaml
external_components:
  - source: github://zacs/esphome-person_detector
    components: [person_detect]
  # Pin to a released tag/commit for reproducible builds, e.g.:
  # - source: github://zacs/esphome-person_detector@v0.1.0
  #   components: [person_detect]
```

### 2. Make sure your device meets the prerequisites

`person_detect` fails codegen with a clear message if these aren't met:

```yaml
esp32:
  variant: ESP32P4          # ESP32-P4 only (v1)
  flash_size: 16MB
  partitions: partitions.csv # app image is ~2 MB — needs a big app partition
  framework:
    type: esp-idf           # ESP-IDF only (no Arduino on P4)
    version: 5.5.4

psram:                      # required — model runtime + frame buffers live here
  mode: hex
  speed: 200MHz
```

Copy [`example/partitions.csv`](example/partitions.csv) next to your config (see
[Memory / flash budget](#memory--flash-budget) for why it's required). If your
device already defines a partition table, widen an app slot to ≥ ~3 MB instead.

### 3. Give it a frame source and wire up the detector

`person_detect` takes frames from exactly one of two backends (see
[Frame source backends](#frame-source-backends)). On the ESP32-P4 / SC2356 the
working one is the built-in **`esp_video_camera`** (CSI + ISP + PPA → RGB888):

```yaml
esp_video_camera:
  id: d1001_cam
  sda: 37           # SC2356 SCCB (sensor I2C)
  scl: 38
  resolution: 1280x720
  rotation: 90      # portrait mount — verify on hardware (BRINGUP.md)
  # power/reset lines live on the PCA9535 expander; see the example

person_detect:
  id: presence
  frame_source_id: d1001_cam   # <- raw backend (not camera_id)

binary_sensor:
  - platform: person_detect
    person_detect_id: presence
    name: "Room Occupied"
```

That's the minimum. Add the optional `sensor` / `switch` / triggers below as
needed. A complete device config (with the expander wiring) is in
[`example/reterminal_d1001.yaml`](example/reterminal_d1001.yaml); a first-flash
verification checklist is in [`BRINGUP.md`](BRINGUP.md).

### Frame source backends

`person_detect` needs exactly one of:

| Key | Backend | Frames | Use when |
|---|---|---|---|
| `frame_source_id` | **`esp_video_camera`** (this repo) | raw RGB888 via CSI+ISP+PPA | ESP32-P4 MIPI-CSI sensors (SC2356 / D1001) — **the working path today** |
| `camera_id` | an ESPHome `camera` | JPEG, decoded on-device | a board that already exposes a JPEG camera through ESPHome's camera framework |

The `camera_id` path exists because the brief asked the detector to *compose*
with ESPHome's camera framework — but that framework, as of 2026.6.0, ships a
C++ base with no user-configurable MIPI-CSI platform, so on the D1001 you use
`esp_video_camera`. Both sit behind the same `FrameSource` seam (DESIGN.md §2).

### Coexisting with an LVGL / display config

The inference model runs on its own low-priority, pinned FreeRTOS task
(`task_priority`, `task_core`), publishes only from the main loop, and keeps
every large buffer in PSRAM — so it stays out of the way of an 800×1280
MIPI-DSI LVGL UI. If your UI task is pinned to a core, pin detection to the
other with `task_core:`.

## Configuration

### `person_detect:` (main component)

```yaml
person_detect:
  id: presence
  camera_id: d1001_cam        # required: an ESPHome camera source
  interval: 1500ms            # inference cadence (default 1500ms, min 100ms)
  confidence_threshold: 60%   # min score to count a detection (default 60%)
  clear_after: 3              # consecutive misses before clearing (default 3)
  model: pedestrian           # selectable model (default: pedestrian)
  # advanced task tuning (defaults shown):
  task_priority: 2            # keep below the UI/LVGL task
  task_stack_size: 8192
  task_core: 1
  on_person_detected:
    - logger.log: "Person detected"
  on_person_cleared:
    - logger.log: "Room empty"
```

### `binary_sensor:`

```yaml
binary_sensor:
  - platform: person_detect
    person_detect_id: presence
    name: "Room Occupied"      # device_class defaults to "occupancy"
    filters:
      - delayed_off: 15s       # optional: on top of clear_after debounce
```

### `sensor:` (optional)

```yaml
sensor:
  - platform: person_detect
    person_detect_id: presence
    type: confidence           # best-box score, 0–100 %
    name: "Presence Confidence"
  - platform: person_detect
    person_detect_id: presence
    type: count                # number of boxes over threshold
    name: "People Count"
```

### `switch:` (optional privacy toggle)

```yaml
switch:
  - platform: person_detect
    person_detect_id: presence
    name: "Presence Detection"  # off => camera idle, no inference (restores on)
```

A full, ready-to-compile D1001 config lives in
[`example/reterminal_d1001.yaml`](example/reterminal_d1001.yaml).

## Memory / flash budget

All large buffers are placed in the shared PSRAM; internal SRAM use is limited to
the single inference-task stack, so this coexists with a heavy 800×1280
MIPI-DSI LVGL display.

**Measured static footprint** (CI `esphome compile`, ESP32-P4, ESPHome 2026.6.0 /
ESP-IDF 5.5.4, CSI backend `esp_video_camera` + `person_detect` + ESP-DL +
pedestrian model):

| Metric | Value |
|---|---|
| Total image | **2,290,794 B (~2.29 MB)** — **Flash 13.6 %** of 16 MB |
| `.text` (code) | 1.63 MB |
| `.rodata` (model weights + const) | 596 KB |
| Internal RAM (DIRAM) | 84,942 B — **12.96 %** of 640 KB (`.bss` 19 KB, `.data` 10 KB) |
| Static RAM total | 5.8 % of 512 KB |
| PPA / capture / tensor-arena buffers | **PSRAM, allocated at runtime** (not in the image) |

**Flash / partition table (required).** The pedestrian model is embedded in the
app image (flash rodata), so the linked binary is **~2.3 MB** — over ESPHome's
default ~1.75 MB app partition. You **must** use a partition table with a larger
app partition. The example ships one (`example/partitions.csv`, 6 MB OTA slots on
16 MB flash) wired via:

```yaml
esp32:
  flash_size: 16MB
  partitions: partitions.csv
```

The PSRAM figures depend on capture resolution/rotation and can only be read on
hardware — **measure them on your own build**:

- **Flash** — the `esphome compile` size output reports the app image size
  (RAM/Flash summary). The pedestrian model dominates the delta.
- **PSRAM** — the component logs `model runtime PSRAM cost` at startup and a
  `PSRAM free low-water` figure (an arena high-water proxy) in `dump_config`.
  Watch these in the logs.

| Item | Placement | Notes |
|---|---|---|
| Pedestrian model weights | Flash (rodata) | measure via link map |
| ESP-DL runtime / tensor arena | **PSRAM** | logged as "model runtime PSRAM cost" |
| Decoded RGB888 frame | **PSRAM** | W×H×3 (1280×720 ≈ 2.6 MB), freed each cycle |
| Camera frame buffers | **PSRAM** | owned by the camera component |
| Inference task stack | Internal SRAM | `task_stack_size` (default 8 KiB) |
| Added IRAM | — | none beyond ESP-DL's own |

## Tuning guide

- **`confidence_threshold`** — raise it (e.g. 70–80 %) if you get false positives
  from clutter; lower it (e.g. 45–55 %) if real people are missed. It gates both
  the binary sensor and the count.
- **`interval`** — presence does not need video framerates. 1–2 s is a good
  default; longer intervals cut CPU/heat and battery use at the cost of latency.
  Inference itself is ~55 ms on the P4, so the task is idle most of the interval.
- **`clear_after`** + **`delayed_off`** — `clear_after` requires N consecutive
  negative inferences before dropping presence (debounce against single-frame
  misses); add a `delayed_off:` filter for occupancy-style hold-open behavior.
  Together they stop the sensor flickering when someone briefly turns away.
- **Lighting (SC2356 caveats)** — the SC2356 is a small 2 MP sensor; in low
  light, gain/noise rise and detection confidence drops. Prefer even, front-lit
  scenes; avoid strong backlight (windows behind the subject) which silhouettes
  people and hurts detection. If you see confidence sag at dusk, lower the
  threshold on a schedule or add supplemental lighting.
- **Task placement** — `task_priority` is intentionally low (2). If you run a
  heavy LVGL UI, keep the UI task on one core and, if needed, pin detection to
  the other via `task_core`.

## Diagnostics

Set `logger: level: DEBUG` to see, per inference: wall-clock inference time,
frame dimensions, box count, best score, and free PSRAM. `dump_config` prints the
model, cadence, threshold, task placement, model PSRAM cost, PSRAM free
low-water, last inference time, and capture-failure count.

## How it works (short)

`PersonDetector` pulls one typed RGB frame per `interval` from a `FrameSource`
(on a dedicated low-priority FreeRTOS task), runs an ESP-DL detection model on
it (resized to the model input via the P4's hardware image path), and marshals
the result back to the ESPHome main loop, where debouncing, state publishing,
and triggers happen. Frames come from a pluggable backend — `esp_video_camera`
(raw MIPI-CSI) or an ESPHome JPEG camera. See [`DESIGN.md`](DESIGN.md) for the
full design, verified upstream API references, memory plan, and open risks.

## Extending

The component is deliberately layered so contributors can add support without
touching the core:

- **Add a detection model.** Models are selectable via `model:` on
  `person_detect`. Adding one is a row in the `MODELS` table in
  `components/person_detect/__init__.py` plus a `case` in
  `PersonDetector::create_model_()` — any ESP-DL detector returning
  `dl::detect::result_t` boxes fits the existing pipeline.
- **Add a camera sensor.** `esp_video_camera` selects the sensor via Kconfig; a
  new MIPI-CSI sensor is a `sensor:` enum entry + its `esp_cam_sensor` Kconfig
  in `components/esp_video_camera/__init__.py`. The capture/PPA/rotate path is
  sensor-independent.
- **Add a frame-source backend.** Implement `person_detect::FrameSource`
  (`frame_source.h`) — yield typed frames (`FrameView`) and the detector
  consumes them unchanged. This is how `esp_video_camera` (raw) and
  `EsphomeCameraSource` (JPEG) coexist.
- **Add a board.** A board is just a device YAML: its `esp32:` target details
  (e.g. silicon revision, ESP-Hosted pins), its camera wiring, and a
  `person_detect` block. The reTerminal D1001 files under `example/` and
  `firmware/` are the first such board; others sit alongside them.

Contributions for new models, sensors, and boards are welcome.

## License

C++ under **GPL-3.0-only** to match the ESPHome ecosystem (see `LICENSE`).
Bundled Espressif ESP-DL and the pedestrian model are Apache-2.0 / MIT — see
`NOTICE`.
