#pragma once

// Test-only stub: satisfies esphome::camera::Camera so person_detect can be
// validated and compiled in CI without a real camera platform. It registers as
// a camera but never delivers frames. NOT for production use.

#include "esphome/core/component.h"
#include "esphome/components/camera/camera.h"

namespace esphome {
namespace fake_camera {

class FakeCamera : public Component, public camera::Camera {
 public:
  void add_listener(camera::CameraListener *listener) override {}
  camera::CameraImageReader *create_image_reader() override { return nullptr; }
  void request_image(camera::CameraRequester requester) override {}
  void start_stream(camera::CameraRequester requester) override {}
  void stop_stream(camera::CameraRequester requester) override {}
};

}  // namespace fake_camera
}  // namespace esphome
