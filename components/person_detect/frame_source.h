#pragma once

#include <cstdint>

namespace esphome {
namespace person_detect {

// Pixel layout of a FrameView. Kept ESP-DL-free so backends don't need ESP-DL
// headers; the detector maps this to dl::image::pix_type_t in one place.
enum FrameFormat : int {
  FRAME_FORMAT_RGB888 = 0,  // 24-bit, R,G,B byte order
  FRAME_FORMAT_RGB565 = 1,  // 16-bit little-endian
  FRAME_FORMAT_GRAYSCALE = 2,
};

// A single frame handed to the detector, valid only until FrameSource::release().
struct FrameView {
  const uint8_t *data{nullptr};
  uint16_t width{0};
  uint16_t height{0};
  FrameFormat format{FRAME_FORMAT_RGB888};
};

// Abstraction over "where frames come from". v1 ships EsphomeCameraSource, which
// consumes an ESPHome camera by ID and owns no hardware. A direct esp_video /
// esp_cam_ctlr backend (owning the sensor) can implement this same interface
// later for boards ESPHome's camera framework does not yet support — see
// DESIGN.md §2 / §7. The detector only ever talks to this interface.
class FrameSource {
 public:
  virtual ~FrameSource() = default;

  // One-time wiring (register listeners, allocate sync primitives). Returns
  // false if the source cannot be used (e.g. missing camera).
  virtual bool init() = 0;

  // Begin / end frame delivery. stop() must fully idle the camera so the device
  // does no capture work while detection is disabled (privacy).
  virtual bool start() = 0;
  virtual void stop() = 0;

  // Block up to timeout_ms for the next frame. On success fills `out` with a
  // buffer valid until release() and returns true; returns false on timeout.
  virtual bool acquire(FrameView &out, uint32_t timeout_ms) = 0;

  // Release the frame returned by the last successful acquire(). Exactly one
  // frame may be in flight at a time.
  virtual void release() = 0;
};

}  // namespace person_detect
}  // namespace esphome
