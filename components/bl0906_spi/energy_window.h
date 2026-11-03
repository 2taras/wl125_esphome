#pragma once

#include <algorithm>
#include <cstdint>

namespace esphome {
namespace bl0906_spi {

// Integrate signed instantaneous U*I over complete mains periods before
// deciding import/export. Negative samples within a period are reactive
// exchange, not necessarily export of active energy.
class BL0906EnergyWindow {
 public:
  double integrate(double previous_w, double current_w, uint32_t elapsed_us, uint32_t period_us) {
    if (elapsed_us == 0 || period_us == 0)
      return 0.0;
    uint32_t used_us = 0;
    double imported_kwh = 0.0;
    while (used_us < elapsed_us) {
      // Freeze the period for an open window; frequency updates apply to the
      // next one and cannot truncate or extend an already accumulated cycle.
      if (this->duration_us_ == 0)
        this->period_us_ = period_us;
      const uint32_t part_us = std::min(elapsed_us - used_us, this->period_us_ - this->duration_us_);
      const double start_w = previous_w + (current_w - previous_w) * used_us / elapsed_us;
      const double end_w = previous_w + (current_w - previous_w) * (used_us + part_us) / elapsed_us;
      this->signed_energy_wus_ += (start_w + end_w) * 0.5 * part_us;
      this->duration_us_ += part_us;
      used_us += part_us;
      if (this->duration_us_ == this->period_us_) {
        imported_kwh += std::max(0.0, this->signed_energy_wus_) / 3.6e12;
        this->reset();
      }
    }
    return imported_kwh;
  }

  void reset() {
    this->signed_energy_wus_ = 0.0;
    this->duration_us_ = 0;
  }

 protected:
  double signed_energy_wus_{0.0};
  uint32_t duration_us_{0};
  uint32_t period_us_{0};
};

}  // namespace bl0906_spi
}  // namespace esphome
