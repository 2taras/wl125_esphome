#pragma once

#include <string>

#include "washer_status.h"

namespace esphome::washer_uart {

enum class ReadingBasis { UNAVAILABLE, INTERPOLATED, CALIBRATED_NTC, EXTRAPOLATED, BASELINE };

struct Reading {
  float value;
  ReadingBasis basis;
};

Reading water_temperature(const WasherStatus &status);
Reading water_volume(uint16_t code);
float selected_temperature(uint8_t settings);
const char *reading_basis(ReadingBasis basis);
const char *program_name(uint8_t code);
const char *state_name(uint8_t code);
const char *drying_name(uint8_t settings);
const char *cycle_stage(const WasherStatus &status);
std::string active_options(const WasherStatus &status);

}  // namespace esphome::washer_uart
