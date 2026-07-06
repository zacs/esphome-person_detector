#include "esphome_camera_source.h"

#include "esphome/core/log.h"

#include "esp_heap_caps.h"

// ESP-DL image API: img_t / jpeg_img_t / pix_type + software JPEG decode.
// Types: dl_image_define.hpp; decode helpers: dl_image.hpp. These come from the
// `espressif/pedestrian_detect` -> `esp-dl` dependency pulled in __init__.py.
#include "dl_image.hpp"

namespace esphome {
namespace person_detect {

static const char *const TAG = "person_detect.camera";

bool EsphomeCameraSource::init() {
  if (this->camera_ == nullptr) {
    ESP_LOGE(TAG, "No camera provided to person_detect frame source");
    return false;
  }
  this->frame_ready_ = xSemaphoreCreateBinary();
  if (this->frame_ready_ == nullptr) {
    ESP_LOGE(TAG, "Failed to allocate frame-ready semaphore");
    return false;
  }
  // Receive frames pushed by the camera framework.
  this->camera_->add_listener(this);
  return true;
}

bool EsphomeCameraSource::start() {
  if (this->streaming_)
    return true;
  // Ask the camera to stream. We sample at the detector's cadence and drop the
  // frames in between (see on_camera_image). CameraRequester has no dedicated
  // "consumer" value yet, so we use IDLE (DESIGN.md §7 risk #2).
  this->camera_->start_stream(camera::CameraRequester::IDLE);
  this->streaming_ = true;
  ESP_LOGD(TAG, "Camera stream started");
  return true;
}

void EsphomeCameraSource::stop() {
  if (!this->streaming_)
    return;
  this->camera_->stop_stream(camera::CameraRequester::IDLE);
  this->streaming_ = false;
  // Drop any queued frame so we don't act on a stale image after re-enable.
  {
    std::lock_guard<std::mutex> lock(this->latest_mutex_);
    this->latest_.reset();
  }
  ESP_LOGD(TAG, "Camera stream stopped");
}

void EsphomeCameraSource::on_camera_image(
    const std::shared_ptr<camera::CameraImage> &image) {
  // Runs on the main loop. Keep it cheap: stash the latest frame (dropping any
  // older un-consumed one) and wake the inference task.
  {
    std::lock_guard<std::mutex> lock(this->latest_mutex_);
    this->latest_ = image;
  }
  xSemaphoreGive(this->frame_ready_);
}

bool EsphomeCameraSource::acquire(FrameView &out, uint32_t timeout_ms) {
  if (xSemaphoreTake(this->frame_ready_, pdMS_TO_TICKS(timeout_ms)) != pdTRUE)
    return false;

  {
    std::lock_guard<std::mutex> lock(this->latest_mutex_);
    this->in_flight_ = this->latest_;
    this->latest_.reset();
  }
  if (!this->in_flight_)
    return false;

  uint8_t *buf = this->in_flight_->get_data_buffer();
  size_t len = this->in_flight_->get_data_length();
  if (buf == nullptr || len == 0) {
    ESP_LOGW(TAG, "Empty camera frame");
    this->in_flight_.reset();
    return false;
  }

  // Decode the JPEG bitstream to a fresh RGB888 buffer. sw_/hw_decode_jpeg
  // return an img_t by value with a heap_caps-allocated .data (PSRAM when
  // configured) and width/height read from the JPEG header; .data is nullptr on
  // failure. On ESP32-P4 the hardware JPEG codec is used.
  dl::image::jpeg_img_t jpeg{};
  jpeg.data = buf;
  jpeg.data_len = len;

#if defined(CONFIG_SOC_JPEG_CODEC_SUPPORTED)
  dl::image::img_t decoded =
      dl::image::hw_decode_jpeg(jpeg, dl::image::DL_IMAGE_PIX_TYPE_RGB888);
#else
  dl::image::img_t decoded =
      dl::image::sw_decode_jpeg(jpeg, dl::image::DL_IMAGE_PIX_TYPE_RGB888);
#endif
  if (decoded.data == nullptr) {
    ESP_LOGW(TAG, "JPEG decode failed (len=%u)", (unsigned) len);
    this->in_flight_.reset();
    return false;
  }

  this->decoded_data_ = decoded.data;
  out.data = static_cast<const uint8_t *>(decoded.data);
  out.width = decoded.width;
  out.height = decoded.height;
  out.pix_type = static_cast<int>(dl::image::DL_IMAGE_PIX_TYPE_RGB888);
  return true;
}

void EsphomeCameraSource::release() {
  if (this->decoded_data_ != nullptr) {
    heap_caps_free(this->decoded_data_);
    this->decoded_data_ = nullptr;
  }
  this->in_flight_.reset();
}

}  // namespace person_detect
}  // namespace esphome
