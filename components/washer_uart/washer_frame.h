#pragma once

#include <array>
#include <cstdint>

namespace esphome::washer_uart {

constexpr uint8_t MAX_FRAME_SIZE = 100;

struct FrameView {
  const uint8_t *data;
  uint8_t size;
};

class FrameDecoder {
 public:
  FrameView feed(uint8_t byte) {
    if (byte == FRAME_END) {
      const bool valid = this->receiving_ && !this->escaped_ && this->length_ >= 3 && this->checksum_ == 0;
      const uint8_t length = valid ? this->length_ : 0;
      this->receiving_ = true;
      this->escaped_ = false;
      this->length_ = 0;
      this->checksum_ = 0;
      return {this->frame_.data(), length};
    }
    if (!this->receiving_)
      return {};

    if (this->escaped_) {
      this->escaped_ = false;
      if (byte == 0xDC) {
        byte = FRAME_END;
      } else if (byte == 0xDD) {
        byte = FRAME_ESCAPE;
      } else {
        this->receiving_ = false;
        return {};
      }
    } else if (byte == FRAME_ESCAPE) {
      this->escaped_ = true;
      return {};
    }

    if (this->length_ == this->frame_.size()) {
      this->receiving_ = false;
      return {};
    }
    this->frame_[this->length_++] = byte;
    this->checksum_ += byte;
    return {};
  }

 private:
  static constexpr uint8_t FRAME_END = 0xC0;
  static constexpr uint8_t FRAME_ESCAPE = 0xDB;

  std::array<uint8_t, MAX_FRAME_SIZE> frame_{};
  uint8_t length_{0};
  uint8_t checksum_{0};
  bool receiving_{false};
  bool escaped_{false};
};

}  // namespace esphome::washer_uart
