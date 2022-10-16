#pragma once

#include "esphome/core/component.h"
#include "esphome/core/automation.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/uart/uart.h"

namespace esphome {
namespace wl125 {

class WL125BinarySensor;
class WL125Trigger;

class WL125Component : public Component, public uart::UARTDevice {
 public:
  void loop() override;

  void register_card(WL125BinarySensor *obj) { this->cards_.push_back(obj); }
  void register_trigger(WL125Trigger *trig) { this->triggers_.push_back(trig); }

  float get_setup_priority() const override { return setup_priority::DATA; }

 protected:
  int8_t read_state_{-1};
  uint8_t buffer_[6]{};
  std::vector<WL125BinarySensor *> cards_;
  std::vector<WL125Trigger *> triggers_;
  uint32_t last_id_{0};
};

class WL125BinarySensor : public binary_sensor::BinarySensorInitiallyOff {
 public:
  void set_id(uint32_t id) { id_ = id; }

  bool process(uint32_t id) {
    if (this->id_ == id) {
      this->publish_state(true);
      yield();
      this->publish_state(false);
      return true;
    }
    return false;
  }

 protected:
  uint32_t id_;
};

class WL125Trigger : public Trigger<uint32_t> {
 public:
  void process(uint32_t uid) { this->trigger(uid); }
};

}  // namespace wl125
}  // namespace esphome
