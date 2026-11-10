#pragma once

#include "washer_frame.h"

namespace esphome::washer_uart {

struct WasherStatus {
  uint16_t remaining_time;
  uint16_t drum_speed;
  uint16_t water_level_code;
  uint8_t temperature_code;
  uint8_t program_code;
  uint8_t step_code;
  uint8_t state_code;
  uint8_t error_code;
  uint8_t settings;
  uint8_t flags;
  uint8_t delay_hours;
  bool night_mode;
};

inline bool decode_status(FrameView frame, WasherStatus &status) {
  if (frame.size != 42 || frame.data[0] != 0x0A)
    return false;
  const uint8_t *payload = frame.data + 1;
  const auto read_u16 = [payload](unsigned index) {
    return static_cast<uint16_t>(payload[index] * 256u + payload[index + 1]);
  };
  status = {read_u16(9), read_u16(24), read_u16(19), payload[21], payload[2], payload[18], payload[1],
            payload[3], payload[6], payload[8], payload[32], (payload[38] & 0x08) != 0};
  return true;
}

}  // namespace esphome::washer_uart
