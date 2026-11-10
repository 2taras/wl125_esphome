#pragma once

#include <memory>

#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/uart/uart.h"
#include "esphome/core/component.h"
#include "washer_capture.h"
#include "washer_status.h"

namespace esphome::washer_uart {

class WasherUART : public PollingComponent, public uart::UARTDevice {
 public:
  void setup() override;
  void loop() override;
  void update() override;
  float get_setup_priority() const override { return setup_priority::WIFI - 1.0f; }

#ifdef USE_WASHER_CAPTURE
  void enable_capture() { this->capture_enabled_ = true; }
  void set_display_uart(uart::UARTComponent *value) { this->display_uart_ = value; }
#endif

  void set_remaining_time(sensor::Sensor *value) { this->remaining_time_ = value; }
  void set_delay_hours(sensor::Sensor *value) { this->delay_hours_ = value; }
  void set_drum_speed(sensor::Sensor *value) { this->drum_speed_ = value; }
  void set_temperature_code(sensor::Sensor *value) { this->temperature_code_ = value; }
  void set_water_level_code(sensor::Sensor *value) { this->water_level_code_ = value; }
  void set_program_code(sensor::Sensor *value) { this->program_code_ = value; }
  void set_step_code(sensor::Sensor *value) { this->step_code_ = value; }
  void set_state_code(sensor::Sensor *value) { this->state_code_ = value; }
  void set_error_code(sensor::Sensor *value) { this->error_code_ = value; }
  void set_water_temperature(sensor::Sensor *value) { this->water_temperature_ = value; }
  void set_selected_temperature(sensor::Sensor *value) { this->selected_temperature_ = value; }
  void set_water_volume(sensor::Sensor *value) { this->water_volume_ = value; }

  void set_program(text_sensor::TextSensor *value) { this->program_ = value; }
  void set_cycle_state(text_sensor::TextSensor *value) { this->cycle_state_ = value; }
  void set_cycle_stage(text_sensor::TextSensor *value) { this->cycle_stage_ = value; }
  void set_door_lock(text_sensor::TextSensor *value) { this->door_lock_ = value; }
  void set_drying_setting(text_sensor::TextSensor *value) { this->drying_setting_ = value; }
  void set_active_options(text_sensor::TextSensor *value) { this->active_options_ = value; }
  void set_water_temperature_basis(text_sensor::TextSensor *value) { this->water_temperature_basis_ = value; }
  void set_water_volume_basis(text_sensor::TextSensor *value) { this->water_volume_basis_ = value; }

 private:
  sensor::Sensor *remaining_time_{nullptr};
  sensor::Sensor *delay_hours_{nullptr};
  sensor::Sensor *drum_speed_{nullptr};
  sensor::Sensor *temperature_code_{nullptr};
  sensor::Sensor *water_level_code_{nullptr};
  sensor::Sensor *program_code_{nullptr};
  sensor::Sensor *step_code_{nullptr};
  sensor::Sensor *state_code_{nullptr};
  sensor::Sensor *error_code_{nullptr};
  sensor::Sensor *water_temperature_{nullptr};
  sensor::Sensor *selected_temperature_{nullptr};
  sensor::Sensor *water_volume_{nullptr};

  text_sensor::TextSensor *program_{nullptr};
  text_sensor::TextSensor *cycle_state_{nullptr};
  text_sensor::TextSensor *cycle_stage_{nullptr};
  text_sensor::TextSensor *door_lock_{nullptr};
  text_sensor::TextSensor *drying_setting_{nullptr};
  text_sensor::TextSensor *active_options_{nullptr};
  text_sensor::TextSensor *water_temperature_basis_{nullptr};
  text_sensor::TextSensor *water_volume_basis_{nullptr};

  FrameDecoder decoder_;
#ifdef USE_WASHER_CAPTURE
  FrameDecoder display_decoder_;
  uart::UARTComponent *display_uart_{nullptr};
  std::unique_ptr<WasherCapture> capture_;
  bool capture_enabled_{false};
#endif
  WasherStatus status_{};
  uint32_t last_status_ms_{0};
  bool has_status_{false};
};

}  // namespace esphome::washer_uart
