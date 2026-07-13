#pragma once

#include "esphome/core/defines.h"

#ifdef USE_SWITCH

#include "esphome/components/switch/switch.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "person_detector.h"

namespace esphome {
namespace person_detect {

// Runtime privacy toggle: on -> detection active, off -> camera idled.
class PersonDetectSwitch : public switch_::Switch, public Parented<PersonDetector> {
 public:
  // Apply the configured restore_mode at boot, like a stock ESPHome switch.
  // Without this the entity never publishes its restored state and, worse, the
  // parented detector's enabled_ stays at its compile-time default (on) — so a
  // switch restored to "off" wouldn't actually gate detection. The detector runs
  // at LATE priority (after this switch), so setting enabled_ here means its
  // setup() sees the right value before it decides whether to start the camera.
  void setup() override {
    auto restored = this->get_initial_state_with_restore();
    if (restored.has_value())
      this->write_state(*restored);
  }

 protected:
  void write_state(bool state) override {
    this->parent_->set_enabled(state);
    this->publish_state(state);
  }
};

}  // namespace person_detect
}  // namespace esphome

#endif  // USE_SWITCH
