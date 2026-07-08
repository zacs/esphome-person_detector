#include "esp_video_camera.h"

#include "esphome/core/hal.h"
#include "esphome/core/log.h"

#include <cerrno>
#include <cinttypes>
#include <cstring>

#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "linux/videodev2.h"

// ESP-IDF's <sys/mman.h> compat shim doesn't always define MAP_FAILED.
#ifndef MAP_FAILED
#define MAP_FAILED (reinterpret_cast<void *>(-1))
#endif

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_video_init.h"

namespace esphome {
namespace esp_video_camera {

static const char *const TAG = "esp_video_camera";
static const char *const VIDEO_DEVICE = "/dev/video0";

static int xioctl(int fd, unsigned long req, void *arg) {
  int r;
  do {
    r = ioctl(fd, req, arg);
  } while (r == -1 && errno == EINTR);
  return r;
}

void EspVideoCamera::setup() {
  ESP_LOGCONFIG(TAG, "Setting up esp_video_camera (MIPI-CSI)...");

  if (!this->power_on_sensor_()) {
    this->mark_failed();
    return;
  }

  // Initialize the CSI + ISP pipeline and the sensor's SCCB (I2C). Power/reset
  // are handled by us via the expander, so tell esp_video there are none. The
  // SCCB gets its OWN I2C controller (sccb_port_) — it must not be the port that
  // ESPHome's `i2c:` bus already owns (e.g. the expander bus), or the master-bus
  // install collides, the sensor never probes, and no /dev/video0 is created.
  esp_video_init_csi_config_t csi = {};
  csi.sccb_config.init_sccb = true;
  csi.sccb_config.i2c_config.port = this->sccb_port_;
  csi.sccb_config.i2c_config.scl_pin = static_cast<gpio_num_t>(this->sccb_scl_);
  csi.sccb_config.i2c_config.sda_pin = static_cast<gpio_num_t>(this->sccb_sda_);
  csi.sccb_config.freq = this->sccb_freq_;
  csi.reset_pin = static_cast<gpio_num_t>(-1);   // handled via expander
  csi.pwdn_pin = static_cast<gpio_num_t>(-1);

  esp_video_init_config_t cfg = {};
  cfg.csi = &csi;
  ESP_LOGCONFIG(TAG, "esp_video_init: SCCB on I2C%d (SDA=%d SCL=%d @ %uHz)",
                this->sccb_port_, this->sccb_sda_, this->sccb_scl_,
                (unsigned) this->sccb_freq_);
  esp_err_t err = esp_video_init(&cfg);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "esp_video_init failed: %s — check SCCB wiring/port and that "
                  "the sensor is powered (enable/reset lines)",
             esp_err_to_name(err));
    this->mark_failed();
    return;
  }

  if (!this->open_and_configure_() || !this->setup_ppa_()) {
    this->mark_failed();
    return;
  }

  this->ready_ = true;
  ESP_LOGCONFIG(TAG, "esp_video_camera ready: capture %ux%u -> rotate %u -> "
                     "RGB888 %ux%u",
                this->cap_w_, this->cap_h_, this->rotation_, this->out_w_,
                this->out_h_);
}

bool EspVideoCamera::power_on_sensor_() {
  // Drive the expander control lines. Polarity is expressed in YAML via the
  // pin's `inverted:` flag, so here we just assert the logical state:
  //   enable = on, power-down = off, then pulse reset.
  if (this->enable_pin_ != nullptr) {
    this->enable_pin_->setup();
    this->enable_pin_->digital_write(true);
  }
  if (this->powerdown_pin_ != nullptr) {
    this->powerdown_pin_->setup();
    this->powerdown_pin_->digital_write(false);
  }
  if (this->reset_pin_ != nullptr) {
    this->reset_pin_->setup();
    this->reset_pin_->digital_write(false);
    delay(10);
    this->reset_pin_->digital_write(true);
    delay(10);
  }
  return true;
}

bool EspVideoCamera::open_and_configure_() {
  // Non-blocking so acquire() can bound its wait with a DQBUF poll loop.
  // esp_video's V4L2 devices don't hook ESP-IDF's VFS select()/poll(), so a
  // select()-gated DQBUF never wakes (the canonical capture_stream example uses
  // a plain blocking DQBUF for exactly this reason).
  this->fd_ = open(VIDEO_DEVICE, O_RDWR | O_NONBLOCK);
  if (this->fd_ < 0) {
    ESP_LOGE(TAG, "open(%s) failed: %s", VIDEO_DEVICE, strerror(errno));
    ESP_LOGE(TAG, "  esp_video_init returned OK but registered no capture device "
                  "— the CSI sensor was not detected over SCCB. Verify the "
                  "SCCB port/pins, that the sensor Kconfig matches the module, "
                  "and that enable/power-down/reset put it in a running state.");
    return false;
  }

  // Ask the ISP to output RGB565 at the capture resolution (2 bytes/px keeps
  // the capture buffers small; the PPA converts to RGB888 while rotating).
  struct v4l2_format fmt = {};
  fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  fmt.fmt.pix.width = this->cap_w_;
  fmt.fmt.pix.height = this->cap_h_;
  fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_RGB565;
  fmt.fmt.pix.field = V4L2_FIELD_NONE;
  if (xioctl(this->fd_, VIDIOC_S_FMT, &fmt) != 0) {
    ESP_LOGE(TAG, "VIDIOC_S_FMT failed: %s", strerror(errno));
    return false;
  }
  // The driver may adjust the geometry/format; honor what it gave us and flag a
  // format it couldn't satisfy (the PPA pass below assumes RGB565 input).
  this->cap_w_ = fmt.fmt.pix.width;
  this->cap_h_ = fmt.fmt.pix.height;
  if (fmt.fmt.pix.pixelformat != V4L2_PIX_FMT_RGB565) {
    ESP_LOGW(TAG, "Requested RGB565 but driver set pixelformat 0x%08" PRIx32
                  " (%ux%u) — capture may not match PPA input",
             (uint32_t) fmt.fmt.pix.pixelformat, this->cap_w_, this->cap_h_);
  }

  struct v4l2_requestbuffers req = {};
  req.count = this->fb_count_;
  req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  req.memory = V4L2_MEMORY_MMAP;
  if (xioctl(this->fd_, VIDIOC_REQBUFS, &req) != 0) {
    ESP_LOGE(TAG, "VIDIOC_REQBUFS failed: %s", strerror(errno));
    return false;
  }

  this->buffers_.resize(req.count);
  for (uint32_t i = 0; i < req.count; i++) {
    struct v4l2_buffer buf = {};
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = i;
    if (xioctl(this->fd_, VIDIOC_QUERYBUF, &buf) != 0) {
      ESP_LOGE(TAG, "VIDIOC_QUERYBUF[%u] failed: %s", i, strerror(errno));
      return false;
    }
    this->buffers_[i].length = buf.length;
    this->buffers_[i].start = mmap(nullptr, buf.length, PROT_READ | PROT_WRITE,
                                   MAP_SHARED, this->fd_, buf.m.offset);
    if (this->buffers_[i].start == MAP_FAILED) {
      ESP_LOGE(TAG, "mmap[%u] failed: %s", i, strerror(errno));
      return false;
    }
    // Queue it for capture.
    if (xioctl(this->fd_, VIDIOC_QBUF, &buf) != 0) {
      ESP_LOGE(TAG, "VIDIOC_QBUF[%u] failed: %s", i, strerror(errno));
      return false;
    }
  }
  return true;
}

bool EspVideoCamera::setup_ppa_() {
  // Output geometry: 90/270 swap width and height.
  if (this->rotation_ == 90 || this->rotation_ == 270) {
    this->out_w_ = this->cap_h_;
    this->out_h_ = this->cap_w_;
  } else {
    this->out_w_ = this->cap_w_;
    this->out_h_ = this->cap_h_;
  }

  ppa_client_config_t pc = {};
  pc.oper_type = PPA_OPERATION_SRM;
  esp_err_t err = ppa_register_client(&pc, &this->ppa_);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "ppa_register_client failed: %s", esp_err_to_name(err));
    return false;
  }

  // PPA output buffer in PSRAM, aligned for L1/L2 cache as the PPA requires.
  this->rotated_size_ =
      (static_cast<size_t>(this->out_w_) * this->out_h_ * 3 + 127) & ~static_cast<size_t>(127);
  this->rotated_ =
      heap_caps_aligned_alloc(128, this->rotated_size_, MALLOC_CAP_SPIRAM);
  if (this->rotated_ == nullptr) {
    ESP_LOGE(TAG, "Failed to allocate %u-byte PPA output buffer in PSRAM",
             (unsigned) this->rotated_size_);
    return false;
  }
  return true;
}

bool EspVideoCamera::init() { return this->ready_; }

bool EspVideoCamera::start() {
  if (!this->ready_ || this->streaming_)
    return this->ready_;
  int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  if (xioctl(this->fd_, VIDIOC_STREAMON, &type) != 0) {
    ESP_LOGE(TAG, "VIDIOC_STREAMON failed: %s", strerror(errno));
    return false;
  }
  this->streaming_ = true;
  ESP_LOGD(TAG, "Streaming started");
  return true;
}

void EspVideoCamera::stop() {
  if (!this->streaming_)
    return;
  int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  xioctl(this->fd_, VIDIOC_STREAMOFF, &type);
  this->streaming_ = false;
  ESP_LOGD(TAG, "Streaming stopped");
}

bool EspVideoCamera::acquire(person_detect::FrameView &out, uint32_t timeout_ms) {
  if (!this->streaming_)
    return false;

  // The fd is non-blocking (esp_video doesn't support select/poll), so poll
  // DQBUF until a filled buffer is ready or the timeout elapses. At 30 fps a
  // frame lands within ~33 ms; the short vTaskDelay yields to other tasks.
  struct v4l2_buffer buf = {};
  buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  buf.memory = V4L2_MEMORY_MMAP;
  const int64_t deadline_us = esp_timer_get_time() + (int64_t) timeout_ms * 1000;
  int rc;
  while ((rc = xioctl(this->fd_, VIDIOC_DQBUF, &buf)) != 0 && errno == EAGAIN) {
    if (esp_timer_get_time() >= deadline_us) {
      this->capture_failures_++;
      return false;
    }
    vTaskDelay(pdMS_TO_TICKS(2));
  }
  if (rc != 0) {
    ESP_LOGW(TAG, "VIDIOC_DQBUF failed: %s", strerror(errno));
    this->capture_failures_++;
    return false;
  }
  this->dq_index_ = buf.index;

  // Rotate (portrait mount) + RGB565->RGB888 in one PPA pass.
  ppa_srm_rotation_angle_t angle = PPA_SRM_ROTATION_ANGLE_0;
  switch (this->rotation_) {
    case 90:
      angle = PPA_SRM_ROTATION_ANGLE_90;
      break;
    case 180:
      angle = PPA_SRM_ROTATION_ANGLE_180;
      break;
    case 270:
      angle = PPA_SRM_ROTATION_ANGLE_270;
      break;
    default:
      angle = PPA_SRM_ROTATION_ANGLE_0;
      break;
  }

  ppa_srm_oper_config_t op = {};
  op.in.buffer = this->buffers_[this->dq_index_].start;
  op.in.pic_w = this->cap_w_;
  op.in.pic_h = this->cap_h_;
  op.in.block_w = this->cap_w_;
  op.in.block_h = this->cap_h_;
  op.in.block_offset_x = 0;
  op.in.block_offset_y = 0;
  op.in.srm_cm = PPA_SRM_COLOR_MODE_RGB565;
  op.out.buffer = this->rotated_;
  op.out.buffer_size = this->rotated_size_;
  op.out.pic_w = this->out_w_;
  op.out.pic_h = this->out_h_;
  op.out.block_offset_x = 0;
  op.out.block_offset_y = 0;
  op.out.srm_cm = PPA_SRM_COLOR_MODE_RGB888;
  op.rotation_angle = angle;
  op.scale_x = 1.0f;
  op.scale_y = 1.0f;
  op.rgb_swap = this->swap_rgb_;
  op.byte_swap = false;
  op.mode = PPA_TRANS_MODE_BLOCKING;

  int64_t t0 = esp_timer_get_time();
  esp_err_t err = ppa_do_scale_rotate_mirror(this->ppa_, &op);
  this->last_ppa_us_ = static_cast<uint32_t>(esp_timer_get_time() - t0);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "PPA rotate/convert failed: %s", esp_err_to_name(err));
    this->release();  // requeue the buffer
    return false;
  }

  out.data = static_cast<const uint8_t *>(this->rotated_);
  out.width = this->out_w_;
  out.height = this->out_h_;
  out.format = person_detect::FRAME_FORMAT_RGB888;
  return true;
}

void EspVideoCamera::release() {
  if (this->dq_index_ < 0)
    return;
  struct v4l2_buffer buf = {};
  buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  buf.memory = V4L2_MEMORY_MMAP;
  buf.index = this->dq_index_;
  xioctl(this->fd_, VIDIOC_QBUF, &buf);
  this->dq_index_ = -1;
}

void EspVideoCamera::dump_config() {
  ESP_LOGCONFIG(TAG, "esp_video_camera (MIPI-CSI):");
  ESP_LOGCONFIG(TAG, "  SCCB (sensor I2C): I2C%d SDA=%d SCL=%d @ %uHz",
                this->sccb_port_, this->sccb_sda_, this->sccb_scl_,
                (unsigned) this->sccb_freq_);
  LOG_PIN("  Enable pin: ", this->enable_pin_);
  LOG_PIN("  Power-down pin: ", this->powerdown_pin_);
  LOG_PIN("  Reset pin: ", this->reset_pin_);
  ESP_LOGCONFIG(TAG, "  Capture: %ux%u RGB565, %u buffers", this->cap_w_,
                this->cap_h_, this->fb_count_);
  ESP_LOGCONFIG(TAG, "  Rotation: %u deg -> output %ux%u RGB888 (swap_rgb=%s)",
                this->rotation_, this->out_w_, this->out_h_,
                YESNO(this->swap_rgb_));
  ESP_LOGCONFIG(TAG, "  PPA output buffer: %u bytes PSRAM",
                (unsigned) this->rotated_size_);
  if (this->last_ppa_us_ != 0)
    ESP_LOGCONFIG(TAG, "  Last PPA rotate: %u us", (unsigned) this->last_ppa_us_);
  ESP_LOGCONFIG(TAG, "  Capture failures: %u", (unsigned) this->capture_failures_);
  if (this->is_failed())
    ESP_LOGE(TAG, "  Component failed to set up");
}

}  // namespace esp_video_camera
}  // namespace esphome
