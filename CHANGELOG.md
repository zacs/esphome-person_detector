# Changelog

## v0.1.7 — capture frames with a blocking DQBUF (no select/poll)

v0.1.6 detected the SC202CS, opened `/dev/video0`, and started streaming, but
every capture timed out: `esp_video`'s V4L2 devices don't register ESP-IDF's VFS
`select()`/`poll()` hooks, so the `select()`-gated `DQBUF` in `acquire()` never
saw the fd become readable and always hit the timeout — even though frames were
being produced. (Espressif's own `capture_stream` example uses a plain blocking
`DQBUF` for exactly this reason.)

- Open the device **non-blocking** and poll `VIDIOC_DQBUF` (EAGAIN → short
  `vTaskDelay`) until a filled buffer is ready or the timeout elapses. Drops the
  `select()` path entirely.
- Warn if the driver couldn't honor the requested RGB565 capture format (the PPA
  rotate/convert pass assumes RGB565 input).

## v0.1.6 — correct sensor driver (SC202CS) + current esp_video

v0.1.5 put the SCCB on its own controller but the SC2356 still wasn't detected:
`esp_video_init` returned OK, logged nothing, and created no `/dev/video0`. Root
cause was the sensor driver never being compiled/registered — two version issues,
verified against `espressif/esp-video-components` rather than guessed:

- **Driver rename.** esp_cam_sensor renamed this part **SC2356 → SC202CS** (same
  die). Our `CONFIG_CAMERA_SC2356*` keys didn't exist in the resolved driver, and
  ESP-IDF silently ignores unknown sdkconfig keys, so no sensor driver was built.
  Now sets `CONFIG_CAMERA_SC202CS` + the `SC202CS` RAW8 1280×720 default-format.
- **Auto-detect.** `esp_video_init` only probes sensors whose auto-detect fn is
  registered — now enables `CONFIG_CAMERA_SC202CS_AUTO_DETECT_MIPI_INTERFACE_SENSOR`.
- **Bumped `espressif/esp_video` 0.9.0 → 2.2.0.** The old pin predated ESP-IDF
  5.5.4 (what ESPHome 2026.6 uses) and the SC202CS naming. The `esp_video_init`
  CSI/SCCB API is unchanged across the bump, so no component C++ changed.
- Also enables `CONFIG_ESP_VIDEO_ENABLE_ISP_VIDEO_DEVICE` (the ISP pipeline
  controller, which demosaics the sensor's RAW8 Bayer to RGB, depends on it).

## v0.1.5 — camera SCCB on its own I2C controller (sensor now detectable)

First hardware boot of v0.1.4 got the whole ESPHome app up (Wi-Fi AP, xl9535,
PSRAM all fine) but `esp_video_camera` failed with
`open(/dev/video0): No such file or directory` — `esp_video_init` returned OK yet
registered no capture device, i.e. the SC2356 was never detected over SCCB.

- **Fix:** the sensor SCCB was hard-coded to **I2C port 0**, the same controller
  ESPHome assigns to the first `i2c:` bus — on the D1001 that's the GPIO20/21
  expander bus. Two masters on one controller collide, the sensor never probes,
  and no `/dev/video0` is created. The SCCB now uses a **dedicated controller**
  via a new `i2c_port:` option (default **1**), independent of the expander bus.
- Clearer diagnostics: log the SCCB port at setup, and on an `open()` miss say
  explicitly that the sensor wasn't detected (with what to check).

## v0.1.4 — camera pinout verified against the D1001 schematic

Cross-checked the camera wiring against the official reTerminal D1001 schematic
(Seeed, KiCad `LCD_CAM` sheet) instead of trusting derived values:

- **Confirmed:** SCCB on **GPIO37 (SDA) / GPIO38 (SCL)** (shared peripheral I2C,
  camera addr 0x10); expander controls **CAM_EN=EXP_GPO1 (xl9535 pin 1),
  CAM_PWDN=EXP_GPO3 (pin 3), CAM_RST=EXP_GPO9=P1.1 (pin 11)**; MCLK is a board
  24 MHz source (no xclk driven by the SoC).
- **Fix:** the reset line is active-low at the sensor and the driver pulses the
  expander pin low→high to release, so the expander pin must **not** be
  `inverted:` — removing it (v0.1.3 would have held the sensor in reset).

## v0.1.3 — adopt canonical reTerminal D1001 board config

Replace hand-derived board bring-up with the known-good reTerminal D1001 config
from [zacs/espcontrol](https://github.com/zacs/espcontrol)
(`devices/seeed-esp32-p4-reterminal-d1001/device/device.yaml`), fixing several
guessed values at once:

- `cpu_frequency: 360MHZ`, and framework `advanced: execute_from_psram: true` +
  experimental features + PSRAM/hosted sdkconfig (`ESP_HOSTED_DFLT_TASK_FROM_SPIRAM`,
  `MBEDTLS_EXTERNAL_MEM_ALLOC`, `SDMMC_HOST_DEFAULT`, 60 s task WDT).
- `esp32_hosted: sdio_frequency: 20MHz`.
- Camera power/reset expander corrected to **`xl9535` @ 0x20 on I2C GPIO20/GPIO21**
  (was a guessed `pca9554` on pins that collided with the ESP-Hosted SDIO bus).
- **Drop the custom partition CSV** — `flash_size: 16MB` lets ESPHome generate a
  self-consistent table + factory image (removes the offset class of bug in v0.1.2).

All device-config; no component code changed.

## v0.1.2 — partition-offset fix (no bootable app)

- **Fix `No bootable app partitions` / `invalid magic byte` on boot.** The
  two-OTA partition tables (firmware + example) started the first app slot at
  0x20000, but ESPHome's factory image writes the app at the standard 0x10000,
  so the bootloader found an empty slot. Realign to the standard ESP-IDF two-OTA
  layout (app at 0x10000). Device-config detail, not a component change.

## v0.1.1 — pre-rev3 ESP32-P4 boot fix

- **Fix boot-loop / `Illegal instruction` at bootloader entry** on pre-rev3
  ESP32-P4 silicon (ROM `esp32p4-eco2`; the reTerminal D1001 ships this): the
  build defaulted to rev3 production silicon. The board configs now set
  `esp32: engineering_sample: true` (engineering board + safe 360 MHz CPU). This
  is a per-board device-config detail, not a component change.

## v0.1.0 — initial release

`person_detect`: general-purpose on-device human-presence detection for ESPHome,
exposed to Home Assistant as an occupancy `binary_sensor`. Verified to **compile
and link for ESP32-P4** in CI (ESPHome 2026.6.0 / ESP-IDF 5.5.4). The reTerminal
D1001 is the first supported board. Not yet hardware-verified — see `BRINGUP.md`.

### Added
- `person_detect` component: ESP-DL `PedestrianDetect` (224×224 INT8) on a
  low-priority pinned FreeRTOS task; debounced occupancy `binary_sensor`,
  optional confidence/count `sensor`s, privacy `switch`, and
  `on_person_detected` / `on_person_cleared` triggers.
- `esp_video_camera` component: generic ESP32-P4 MIPI-CSI capture backend
  (`esp_video`/V4L2 + ISP → PPA hardware rotate + RGB565→RGB888 → ESP-DL);
  sensor selectable via Kconfig (first sensor: SC2356), `rotation:`/`swap_rgb:`.
- Pluggable `FrameSource` seam: `frame_source_id` (raw RGB, `esp_video_camera`)
  and `camera_id` (ESPHome JPEG camera), mutually exclusive.
- Prebuilt reference image for the first supported board (reTerminal D1001,
  `firmware/reterminal_d1001.yaml`) with Wi-Fi AP + captive portal / Improv
  provisioning, published via the Firmware workflow.
- CI: validate (both frame-source paths) + best-effort P4 compile; Firmware
  workflow builds a flashable image and attaches it to tagged releases.

### Measured (CI link, CSI build)
- Image ~2.29 MB (Flash 13.6% of 16 MB; `.rodata`/model 596 KB); internal RAM
  5.8%. Capture/PPA/tensor-arena buffers are PSRAM at runtime.

### Known / verify on hardware
- `rotation:` direction, `swap_rgb:` channel order, expander pin polarity + I2C
  bus pins, and the `espressif/esp_video` version pin (`0.9.0`).
