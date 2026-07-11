# reTerminal D1001 camera bring-up notes

The D1001 camera path is verified end-to-end on hardware: sensor detection,
capture, exposure, auto-rotation, and pedestrian detection all work with the
`example/`/`firmware/` configs and the prebuilt release image. This file records
what each stage should look like in the logs and the knobs that matter — useful
if you're re-checking a fresh board, or porting to another ESP32-P4 camera board
or sensor. Set `logger: level: DEBUG` and watch the serial console.

## 0. Boot prerequisites
- Pre-rev3 silicon. The D1001's ESP32-P4 is an early stepping (ROM banner
  `esp32p4-eco2`), so the config must set `esp32: engineering_sample: true` —
  without it the rev3 bootloader hits `Illegal instruction` at its entry and
  boot-loops. It also pins a safe 360 MHz CPU.
- `flash_size: 16MB`, no custom partition CSV — ESPHome auto-sizes an app slot
  for the ~2.3 MB image and a matching factory image.
- Wi-Fi/BLE come from the on-board ESP32-C6 over ESP-Hosted (SDIO); the example
  wires it.

## 1. Sensor detected over SCCB
Expected: `esp_video_camera` logs `esp_video_init: SCCB on I2C1 …`, then
`esp_video_camera ready: capture 1280x720 -> …`. If instead you see
`open(/dev/video0): No such file or directory`, `esp_video_init` created no
capture device — the sensor wasn't detected. Things that actually caused this:
- SCCB on the wrong I2C controller. The sensor SCCB (`i2c_port:`, default 1) must
  be a different controller from the ESPHome `i2c:` bus that owns the expander
  (that one is I2C0). Two masters on one controller collide and the probe fails.
- Sensor driver not compiled in. The part is marketed "SC2356" but the
  `esp_cam_sensor` driver is `SC202CS`; the component enables `CONFIG_CAMERA_SC202CS`
  plus its MIPI auto-detect. If you pin a different `esp_video` version, confirm
  those Kconfig names still exist.
- Power/reset. On the D1001 these are XL9535 expander pins: `enable_pin` = 1,
  `powerdown_pin` = 3, `reset_pin` = 11 (active-low at the sensor; the driver
  pulses it low→high, so the expander pin is not `inverted:`). SCCB pins are
  GPIO37/38.

## 2. Frames flow
Expected: no `Frame capture timed out` warnings; `person_detect` logs one line
per inference. The capture loop uses a non-blocking `VIDIOC_DQBUF` poll rather
than `select()`, because esp_video's V4L2 devices don't hook ESP-IDF's VFS
select/poll — a `select()`-gated capture times out forever even though frames are
flowing. If you port the backend, keep the DQBUF poll.

## 3. Exposure (the one that isn't obvious)
The SC202CS powers up at its minimum exposure — a near-black frame — and only
auto-exposes if `esp_ipa` has a tuning config, which isn't wired up. The
component sets exposure/gain explicitly over V4L2 extended controls and
re-asserts them periodically. Expect boot lines like
`sensor exposure=811 (range 8..1244 …)` and `sensor gain=63 (range 0..255 …)`.

Check the `frame[min=… max=… mean=…]` probe on each inference line: a healthy
room reads a `mean` well up from the sensor floor (tens to ~150 depending on
light). If it's stuck near ~10 you're looking at a black frame — raise
`exposure:`/`gain:` (raw sensor units). Too washed out — lower them.

## 4. Orientation
With `rotation: auto`, boot logs `IMU accel ax=… ay=… az=… -> auto rotation N deg`
from the on-board LSM6DS3TR (`0x6A` on the expander bus). The axis→rotation
mapping is calibrated for the D1001 (gravity −Y → 0° landscape, +X → 90°
portrait, +Y → 180°, −X → 270°); rotation is counter-clockwise. Porting to a
board whose IMU sits differently may need that mapping adjusted — the raw
`ax/ay/az` are logged for exactly that. Or skip the IMU and set an explicit
`rotation:`.

## 5. Detection
The model is a full-body pedestrian detector, so frame the camera to see whole
people, not head-and-shoulders. Expected: a person in view gives `boxes>=1` with
`best` around 0.7–0.95 and `present=1`; inference is ~75 ms. If frames are
clearly exposed and upright but confidence is oddly low across the board, the
model may be seeing BGR — try `swap_rgb: true`.

## 6. End-to-end success
- Person enters → `present=1` within ~`interval`; the "Room Occupied"
  `binary_sensor` turns on.
- Person leaves → after `clear_after` consecutive misses (plus any `delayed_off`)
  it turns off.
- Flip the "Presence Detection" switch off → capture stops, presence clears, and
  `psram_free` recovers.
