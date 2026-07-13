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
// Also a Component (not just a Switch) so setup() runs at boot — switch_::Switch
// alone derives from EntityBase and has no setup() hook.
class PersonDetectSwitch : public switch_::Switch,
                           public Component,
                           public Parented<PersonDetector> {
 public:
  // Apply the configured restore_mode at boot, like a stock ESPHome switch.
  // Without this the entity never publishes its restored state and, worse, the
  // parented detector's enabled_ stays at its compile-time default (on) — so a
  // switch restored to "off" wouldn't actually gate detection. This runs at the
  // default (DATA) priority, before the detector's LATE setup(), so enabled_ is
  // set in time for the detector to decide whether to start the camera.
  void setup() override {
    auto restored = this->get_initial_state_with_restore_mode();
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
