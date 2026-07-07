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
 protected:
  void write_state(bool state) override {
    this->parent_->set_enabled(state);
    this->publish_state(state);
  }
};

}  // namespace person_detect
}  // namespace esphome

#endif  // USE_SWITCH
