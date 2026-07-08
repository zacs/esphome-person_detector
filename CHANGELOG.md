# Changelog

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
