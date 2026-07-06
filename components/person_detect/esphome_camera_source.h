#pragma once

#include <memory>
#include <mutex>

#include "frame_source.h"

#include "esphome/components/camera/camera.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace esphome {
namespace person_detect {

// FrameSource backed by an ESPHome camera (the modular camera framework,
// esphome/components/camera). We register as a camera::CameraListener, keep only
// the most recent frame, and decode it to RGB888 on demand.
//
// The shared CameraImage handed to listeners carries JPEG-encoded data (see
// esphome/components/camera/camera.h — the same buffer streamed to the HA API /
// web clients), so acquire() JPEG-decodes it via ESP-DL. If a future camera
// build delivers raw frames to listeners instead, this is the one place to add
// a zero-copy fast path (needs a frame-spec accessor — DESIGN.md §7 risk #1/#2).
class EsphomeCameraSource : public FrameSource, public camera::CameraListener {
 public:
  explicit EsphomeCameraSource(camera::Camera *camera) : camera_(camera) {}

  bool init() override;
  bool start() override;
  void stop() override;
  bool acquire(FrameView &out, uint32_t timeout_ms) override;
  void release() override;

  // camera::CameraListener
  void on_camera_image(const std::shared_ptr<camera::CameraImage> &image) override;

 protected:
  camera::Camera *camera_;
  SemaphoreHandle_t frame_ready_{nullptr};

  std::mutex latest_mutex_;
  // Written by on_camera_image() (main loop), consumed by acquire() (task).
  std::shared_ptr<camera::CameraImage> latest_;
  // Held for the duration of one inference so the buffer stays alive.
  std::shared_ptr<camera::CameraImage> in_flight_;
  // ESP-DL-allocated RGB888 buffer for the decoded frame (PSRAM).
  void *decoded_data_{nullptr};

  bool streaming_{false};
};

}  // namespace person_detect
}  // namespace esphome
