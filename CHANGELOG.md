# Changelog

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
