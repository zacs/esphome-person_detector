# reTerminal D1001 camera bring-up checklist

The `esp_video_camera` backend was written against verified Espressif APIs
(esp_video/V4L2, esp_cam_sensor SC2356, PPA on ESP-IDF v5.5.4) and compiles for
ESP32-P4, but the hardware-specific values below can only be confirmed on a real
D1001. Work through this on first flash; each item says what to watch in the
`DEBUG` logs and how to fix it in YAML.

## 0. Prereqs
- `logger: level: DEBUG`, serial connected.
- `esp_video_camera` `dump_config()` prints capture size, rotation, output dims,
  PPA buffer size. `person_detect` logs one line per inference:
  `inference <ms>: <w>x<h> boxes=<n> best=<score> present=<0/1> psram_free=<b>`.

## 1. esp_video dependency resolves
- If the build fails pulling `espressif/esp_video`, adjust `ESP_VIDEO_REF` in
  `components/esp_video_camera/__init__.py` to a version whose `esp_video_init`
  matches (csi config with `sccb_config.i2c_config` + `dont_init_ldo`).
- If a Kconfig name is rejected (`CONFIG_CAMERA_SC2356*`), check the installed
  `esp_cam_sensor` Kconfig and update the names in the same file.

## 2. Sensor powers up and is detected (SCCB)
- Expect esp_video to log SC2356 detection over SCCB. If it fails:
  - **Power lines.** Confirm the expander mapping (CAM_EN=bit1, PWDN=bit3,
    RST=bit9) and the expander I2C bus pins/address (`i2c:` + `pca9554:` in the
    example). Toggle polarity with `inverted:` on each pin — PWDN is typically
    active-high (high = powered down), RST active-low.
  - **SCCB pins.** `sda: 37 / scl: 38` are the sensor I2C. Verify against your
    board.
  - Try `powerdown_pin`/`enable_pin` inversion first — a sensor held in
    power-down or with CAM_EN low is the most common "not detected".

## 3. Frames flow
- `person_detect` should stop logging "Frame capture timed out". If it keeps
  timing out after detection succeeds, the ISP output format or buffer setup is
  off — confirm the ISP pipeline Kconfig is enabled and try `frame_buffer_count: 3`.

## 4. Orientation (the empirical one)
- The D1001 mounts **portrait**; the sensor is landscape. Stand a person upright
  in view and watch `boxes`/`best`. If detection is weak or misses obvious
  people, cycle `rotation:` through `90` / `270` (and `0`/`180`) — the
  upright-trained model needs the image rotated to match the mounting.
- `dump_config` shows the post-rotation output dims so you can confirm the
  rotate applied (e.g. 1280x720 → 720x1280 at 90°).

## 5. Colour channel order
- If the sensor is detected and frames flow but confidence is oddly low across
  the board, the model may be seeing BGR. Flip `swap_rgb: true`.
- pedestrian_detect expects RGB888; the ISP→PPA path is configured for RGB, but
  ISP RGB byte order varies by sensor tuning — `swap_rgb` is the one knob.

## 6. Success criterion
- Person walks into frame → `present=1` within ~`interval`, `binary_sensor`
  "Room Occupied" turns **on**.
- Person leaves → after `clear_after` consecutive misses (+ any `delayed_off`),
  it turns **off**.
- Flip the "Presence Detection" switch off → capture stops, sensor clears,
  `psram_free` recovers.

## 7. Report back the numbers
Capture from a running board for the README memory table:
- `dump_config`: `model runtime PSRAM cost`, `PPA output buffer`, `PSRAM free
  low-water`, `Last inference` / `Last PPA rotate`.
- `esphome compile` size summary: app image / flash %.
