#include "washer_uart.h"

#include <cmath>
#include <cstdio>
#include <new>

#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include "washer_readings.h"

namespace esphome::washer_uart {

static constexpr uint32_t STATUS_TIMEOUT_MS = 5000;
static constexpr unsigned UART_BYTES_PER_LOOP = 256;

void WasherUART::setup() {
#ifdef USE_WASHER_CAPTURE
  if (!this->capture_enabled_)
    return;
  this->capture_.reset(new (std::nothrow) WasherCapture());
  if (!this->capture_) {
    ESP_LOGE("washer_uart", "Could not allocate the capture ring");
    return;
  }
  auto *server = web_server_base::global_web_server_base;
  server->add_handler(this->capture_.get());
  server->init();
#endif
}

void WasherUART::loop() {
  uint8_t byte;
  for (unsigned count = 0; count < UART_BYTES_PER_LOOP; count++) {
    bool read = this->available() && this->read_byte(&byte);
    if (read) {
      const auto frame = this->decoder_.feed(byte);
      if (decode_status(frame, this->status_)) {
        this->last_status_ms_ = millis();
        this->has_status_ = true;
      }
#ifdef USE_WASHER_CAPTURE
      if (frame.size && this->capture_)
        this->capture_->record(frame, 0);
#endif
    }
#ifdef USE_WASHER_CAPTURE
    if (this->capture_ && this->display_uart_ && this->display_uart_->available() && this->display_uart_->read_byte(&byte)) {
      read = true;
      const auto frame = this->display_decoder_.feed(byte);
      if (frame.size)
        this->capture_->record(frame, 1);
    }
#endif
    if (!read)
      break;
  }
}

void WasherUART::update() {
  if (!this->has_status_)
    return;
  const bool fresh = millis() - this->last_status_ms_ < STATUS_TIMEOUT_MS;
  const auto publish = [fresh](sensor::Sensor *entity, float value) {
    if (entity != nullptr)
      entity->publish_state(fresh ? value : NAN);
  };
  const auto publish_text = [fresh](text_sensor::TextSensor *entity, const char *value) {
    if (entity == nullptr)
      return;
    const char *state = fresh ? value : "Нет связи";
    if (!entity->has_state() || entity->state != state)
      entity->publish_state(state);
  };
  const auto temperature = water_temperature(this->status_);
  const auto volume = water_volume(this->status_.water_level_code);
  const bool off = this->status_.state_code == 0;
  const bool in_cycle = this->status_.state_code == 2 || this->status_.state_code == 3;
  publish(this->remaining_time_, in_cycle ? this->status_.remaining_time : NAN);
  publish(this->display_time_, this->status_.remaining_time);
  publish(this->delay_hours_, off ? NAN : this->status_.delay_hours);
  publish(this->drum_speed_, this->status_.drum_speed);
  publish(this->temperature_code_, this->status_.temperature_code);
  publish(this->water_level_code_, this->status_.water_level_code);
  publish(this->diagnostic_4_, this->status_.diagnostic_4);
  publish(this->diagnostic_5_, this->status_.diagnostic_5);
  publish(this->program_code_, this->status_.program_code);
  publish(this->step_code_, this->status_.step_code);
  publish(this->state_code_, this->status_.state_code);
  publish(this->error_code_, this->status_.error_code);
  publish(this->water_temperature_, temperature.value);
  publish(this->selected_temperature_, off ? NAN : selected_temperature(this->status_.settings));
  publish(this->water_volume_, volume.value);
  publish_text(this->program_, off ? "—" : program_name(this->status_.program_code));
  publish_text(this->cycle_state_, state_name(this->status_.state_code));
  if (this->cycle_stage_ != nullptr) {
    const char *stage = cycle_stage(this->status_);
    char unknown_stage[48];
    if (stage == nullptr) {
      std::snprintf(unknown_stage, sizeof(unknown_stage), "Неизвестный шаг %u",
                    static_cast<unsigned>(this->status_.step_code));
      stage = unknown_stage;
    }
    publish_text(this->cycle_stage_, stage);
  }
  publish_text(this->door_lock_, this->status_.flags & 0x40 ? "Заблокирована" : "Разблокирована");
  publish_text(this->drying_setting_, off ? "—" : drying_name(this->status_.settings));
  if (this->active_options_ != nullptr)
    publish_text(this->active_options_, off ? "—" : active_options(this->status_).c_str());
  publish_text(this->water_temperature_basis_, reading_basis(temperature.basis));
  publish_text(this->water_volume_basis_, reading_basis(volume.basis));
  this->has_status_ = fresh;
}

}  // namespace esphome::washer_uart
