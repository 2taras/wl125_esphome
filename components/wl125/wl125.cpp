#include "wl125.h"
#include "esphome/core/log.h"

namespace esphome {
namespace wl125 {

static const char *const TAG = "wl125";

static const uint8_t WL125_START_BYTE = 0x02;
static const uint8_t WL125_END_BYTE = 0x03;
static const int8_t WL125_STATE_WAITING_FOR_START = -1;

void wl125::WL125Component::loop() {
  while (this->available() > 0) {
    uint8_t data;
    if (!this->read_byte(&data)) {
      ESP_LOGW(TAG, "Reading data from WL125 failed!");
      this->status_set_warning();
      return;
    }

    if (this->read_state_ == WL125_STATE_WAITING_FOR_START) {
      if (data == WL125_START_BYTE) {
        this->read_state_ = 0;
      } else {
        // Not start byte, probably not synced up correctly.
      }
    } else if (this->read_state_ < 11) {
      uint8_t value = (data > '9') ? data - '7' : data - '0';
      if (this->read_state_ % 2 == 0) {
        this->buffer_[this->read_state_ / 2] = value << 4;
      } else {
        this->buffer_[this->read_state_ / 2] += value;
      }
      this->read_state_++;
    } else if (data != WL125_END_BYTE) {
      ESP_LOGW(TAG, "Invalid end byte from WL125!");
      this->read_state_ = WL125_STATE_WAITING_FOR_START;
    } else {
      uint8_t checksum = 0;
      for (uint8_t i = 0; i < 5; i++)
        checksum ^= this->buffer_[i];
      this->read_state_ = WL125_STATE_WAITING_FOR_START;
        // Valid data
        this->status_clear_warning();
        const uint32_t result = encode_uint32(this->buffer_[1], this->buffer_[2], this->buffer_[3], this->buffer_[4]);
        bool report = result != last_id_;
        for (auto *card : this->cards_) {
          if (card->process(result)) {
            report = false;
          }
        }
        for (auto *trig : this->triggers_)
          trig->process(result);

        if (report) {
          ESP_LOGD(TAG, "Found new tag with ID %u", result);
        }
    }
  }
}

}  // namespace wl125
}  // namespace esphome
