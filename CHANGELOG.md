# Changelog

## v0.1.0 — first installable D1001 build

On-device human presence detection for ESP32-P4, exposed to Home Assistant as an
occupancy `binary_sensor`. Verified to **compile and link for ESP32-P4** in CI
(ESPHome 2026.6.0 / ESP-IDF 5.5.4). Not yet hardware-verified — see `BRINGUP.md`.

### Added
- `person_detect` component: ESP-DL `PedestrianDetect` (224×224 INT8) on a
  low-priority pinned FreeRTOS task; debounced occupancy `binary_sensor`,
  optional confidence/count `sensor`s, privacy `switch`, and
  `on_person_detected` / `on_person_cleared` triggers.
- `esp_video_camera` component: real MIPI-CSI capture backend for the SC2356 —
  `esp_video` (V4L2) + ISP → PPA hardware rotate + RGB565→RGB888 → ESP-DL. Owns
  the sensor; power/reset via a PCA9535 expander; `rotation:` and `swap_rgb:`.
- Pluggable `FrameSource` seam: `frame_source_id` (raw RGB, `esp_video_camera`)
  and `camera_id` (ESPHome JPEG camera), mutually exclusive.
- Installable D1001 firmware (`firmware/reterminal_d1001.yaml`) with Wi-Fi AP +
  captive portal / Improv provisioning, published via the Firmware workflow.
- CI: validate (both frame-source paths) + best-effort P4 compile; Firmware
  workflow builds a flashable image and attaches it to tagged releases.

### Measured (CI link, CSI build)
- Image ~2.29 MB (Flash 13.6% of 16 MB; `.rodata`/model 596 KB); internal RAM
  5.8%. Capture/PPA/tensor-arena buffers are PSRAM at runtime.

### Known / verify on hardware
- `rotation:` direction, `swap_rgb:` channel order, expander pin polarity + I2C
  bus pins, and the `espressif/esp_video` version pin (`0.9.0`).
