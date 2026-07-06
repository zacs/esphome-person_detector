#pragma once

#include "esphome/core/automation.h"
#include "person_detector.h"

namespace esphome {
namespace person_detect {

class PersonDetectedTrigger : public Trigger<> {
 public:
  explicit PersonDetectedTrigger(PersonDetector *parent) {
    parent->add_on_detected_callback([this]() { this->trigger(); });
  }
};

class PersonClearedTrigger : public Trigger<> {
 public:
  explicit PersonClearedTrigger(PersonDetector *parent) {
    parent->add_on_cleared_callback([this]() { this->trigger(); });
  }
};

}  // namespace person_detect
}  // namespace esphome
