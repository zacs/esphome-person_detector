#pragma once

// EspVideoCamera — a raw (typed-RGB) FrameSource for ESP32-P4 MIPI-CSI cameras.
//
// Drives the sensor over the P4 CSI + ISP via Espressif's esp_video (V4L2)
// stack, then uses the PPA to rotate (portrait mounting) and convert to RGB888,
// which it hands to person_detect directly — no JPEG encode/decode. This is the
// "raw" backend DESIGN.md §2/§7 anticipated behind the FrameSource seam.
//
// Sensor power/reset lines on the reTerminal D1001 sit on an I2C GPIO expander
// (PCA9535/XL9535), so those are ESPHome GPIOPins (from a pca9554 hub) that we
// drive before esp_video_init; the sensor's own SCCB/I2C is handled by esp_video.

#include "esphome/core/component.h"
#include "esphome/core/gpio.h"
#include "esphome/components/person_detect/frame_source.h"

#include <vector>

#include "driver/ppa.h"

namespace esphome {
namespace esp_video_camera {

enum Rotation : uint16_t {
  ROTATION_0 = 0,
  ROTATION_90 = 90,
  ROTATION_180 = 180,
  ROTATION_270 = 270,
};

class EspVideoCamera : public Component, public person_detect::FrameSource {
 public:
  // Component: bring up the CSI/ISP pipeline before the detector's setup runs.
  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  // person_detect::FrameSource
  bool init() override;
  bool start() override;
  void stop() override;
  bool acquire(person_detect::FrameView &out, uint32_t timeout_ms) override;
  void release() override;

  // Codegen setters
  void set_sccb_pins(int sda, int scl) {
    this->sccb_sda_ = sda;
    this->sccb_scl_ = scl;
  }
  void set_sccb_port(int port) { this->sccb_port_ = port; }
  void set_sccb_freq(uint32_t freq) { this->sccb_freq_ = freq; }
  void set_reset_pin(GPIOPin *pin) { this->reset_pin_ = pin; }
  void set_powerdown_pin(GPIOPin *pin) { this->powerdown_pin_ = pin; }
  void set_enable_pin(GPIOPin *pin) { this->enable_pin_ = pin; }
  void set_capture_size(uint16_t w, uint16_t h) {
    this->cap_w_ = w;
    this->cap_h_ = h;
  }
  void set_rotation(uint16_t deg) { this->rotation_ = deg; }
  void set_swap_rgb(bool swap) { this->swap_rgb_ = swap; }
  void set_frame_buffer_count(uint8_t n) { this->fb_count_ = n; }

 protected:
  bool power_on_sensor_();
  bool open_and_configure_();
  bool setup_ppa_();

  // Config
  int sccb_sda_{-1};
  int sccb_scl_{-1};
  int sccb_port_{1};  // dedicated I2C controller for the sensor SCCB (must not
                      // collide with the ESPHome i2c bus that owns the expander)
  uint32_t sccb_freq_{100000};
  GPIOPin *reset_pin_{nullptr};
  GPIOPin *powerdown_pin_{nullptr};
  GPIOPin *enable_pin_{nullptr};
  uint16_t cap_w_{1280};
  uint16_t cap_h_{720};
  uint16_t rotation_{0};
  bool swap_rgb_{false};
  uint8_t fb_count_{2};

  // Runtime
  bool ready_{false};
  bool streaming_{false};
  int fd_{-1};

  struct MappedBuffer {
    void *start{nullptr};
    size_t length{0};
  };
  std::vector<MappedBuffer> buffers_;
  int dq_index_{-1};  // index of the buffer currently held by acquire()

  ppa_client_handle_t ppa_{nullptr};
  void *rotated_{nullptr};   // PPA output: upright RGB888 (PSRAM, cache-aligned)
  size_t rotated_size_{0};
  uint16_t out_w_{0};        // dims after rotation
  uint16_t out_h_{0};

  uint32_t capture_failures_{0};
  uint32_t last_ppa_us_{0};
};

}  // namespace esp_video_camera
}  // namespace esphome
