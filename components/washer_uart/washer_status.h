#pragma once

#include "washer_frame.h"

namespace esphome::washer_uart {

struct WasherStatus {
  uint16_t remaining_time;
  uint16_t drum_speed;
  uint16_t water_level_code;
  uint16_t diagnostic_4;
  uint16_t diagnostic_5;
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
  const auto read_u16 = [payload](unsigned high, unsigned low) {
    return static_cast<uint16_t>(payload[high] * 256u + payload[low]);
  };
  status = {read_u16(9, 10), read_u16(24, 25), read_u16(19, 20), read_u16(36, 26), read_u16(37, 12),
            payload[21], payload[2], payload[18], payload[1],
            payload[3], payload[6], payload[8], payload[32], (payload[38] & 0x08) != 0};
  return true;
}

}  // namespace esphome::washer_uart
