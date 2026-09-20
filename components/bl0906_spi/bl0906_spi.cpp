#include "bl0906_spi.h"

#include <algorithm>
#include <cmath>

#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

namespace esphome {
namespace bl0906_spi {

static const char *const TAG = "bl0906_spi";

// Keep the energy preference independent of entity names. Entity names are
// user-facing and may change, while the accumulated energy must survive OTA
// updates and renames.
static constexpr uint32_t BL0906_ENERGY_PREFERENCE_KEY = 0xB10906E1UL;
static constexpr uint32_t BL0906_LEGACY_PREFERENCE_SALT = 0xB1090601UL;

static constexpr uint8_t BL0906_SPI_READ = 0x82;
static constexpr uint8_t BL0906_I_WAVE[BL0906_CHANNEL_COUNT] = {0x02, 0x03, 0x04, 0x05, 0x08, 0x09};
static constexpr uint8_t BL0906_V_WAVE = 0x0B;
static constexpr uint8_t BL0906_PERIOD = 0x4E;
static constexpr uint8_t BL0906_TEMPERATURE = 0x5E;

void BL0906WaveformChannelSelect::control(size_t index) {
  if (this->parent_ == nullptr || index >= BL0906_CHANNEL_COUNT)
    return;
  this->parent_->set_waveform_channel(index);
  this->publish_state(index);
}

void BL0906WaveformCaptureButton::press_action() {
  if (this->parent_ != nullptr)
    this->parent_->capture_waveform();
}

int32_t BL0906SPI::sign_extend_24_(uint32_t value) {
  value &= 0x00FFFFFFUL;
  if ((value & 0x00800000UL) != 0)
    value |= 0xFF000000UL;
  return static_cast<int32_t>(value);
}

void BL0906SPI::setup() {
  ESP_LOGCONFIG(TAG, "Setting up BL0906 SPI waveform meter...");

  if (this->select_pin_ == nullptr || this->reset_pin_ == nullptr) {
    ESP_LOGE(TAG, "select_pin and reset_pin are required");
    this->mark_failed();
    return;
  }

  // Hold BL0906 in reset while selecting SPI. spi_setup() also drives /CS
  // high; the SPI bus drives SCLK low because this device uses Mode 1.
  this->reset_pin_->setup();
  this->reset_pin_->digital_write(false);
  this->select_pin_->setup();
  this->select_pin_->digital_write(true);
  this->spi_setup();
  delay(2);
  this->reset_pin_->digital_write(true);
  delay(10);

  // Six 0xFF bytes reset only the chip's SPI interface state machine.
  uint8_t reset_frame[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  this->enable();
  this->write_array(reset_frame, sizeof(reset_frame));
  this->disable();

  this->sample_interval_us_ = static_cast<uint32_t>(1000000.0f / this->requested_sample_rate_hz_);
  this->sample_interval_us_ = std::max<uint32_t>(500, this->sample_interval_us_);
  // Enough samples on both sides for any allowed +/-180 degree shift at
  // the lowest supported line frequency (45 Hz), plus interpolation margin.
  this->lookahead_frames_ = static_cast<uint16_t>(std::ceil(this->requested_sample_rate_hz_ / 90.0f)) + 4;
  this->lookahead_frames_ = std::max<uint16_t>(8, std::min<uint16_t>(48, this->lookahead_frames_));

  if (this->restore_energy_) {
    sensor::Sensor *key_sensor = nullptr;
    for (auto *sensor : this->energy_sensors_) {
      if (sensor != nullptr) {
        key_sensor = sensor;
        break;
      }
    }
    if (key_sensor != nullptr) {
      this->energy_preference_ = global_preferences->make_preference<BL0906EnergyRestoreState>(
          BL0906_ENERGY_PREFERENCE_KEY);
      BL0906EnergyRestoreState restored;
      bool loaded = this->energy_preference_.load(&restored);

      // Migrate the preference format used before the key became stable. The
      // standard Grid L1 name covers existing deployed meters; the current
      // first energy sensor covers custom configurations.
      if (!loaded) {
        const uint32_t deployed_legacy_key =
            fnv1_hash("grid_l1_corrected_energy") ^ BL0906_LEGACY_PREFERENCE_SALT;
        auto deployed_legacy =
            global_preferences->make_preference<BL0906EnergyRestoreState>(deployed_legacy_key);
        loaded = deployed_legacy.load(&restored);
      }
      if (!loaded) {
        const uint32_t current_legacy_key =
            key_sensor->get_object_id_hash() ^ BL0906_LEGACY_PREFERENCE_SALT;
        auto current_legacy =
            global_preferences->make_preference<BL0906EnergyRestoreState>(current_legacy_key);
        loaded = current_legacy.load(&restored);
      }

      if (loaded) {
        this->energy_kwh_ = restored.energy_kwh;
        ESP_LOGI(TAG, "Restored corrected energy counters");
        // Store immediately under the stable key so later renames no longer
        // affect recovery.
        this->energy_preference_.save(&restored);
      }
      this->preference_ready_ = true;
      this->set_interval("save_energy", 300000, [this]() { this->save_energy_(); });
    }
  }

  this->publish_window_start_ms_ = millis();
  this->next_sample_us_ = micros() + this->sample_interval_us_;
  this->read_frequency_();
  this->read_temperature_();
  if (this->waveform_channel_select_ != nullptr)
    this->waveform_channel_select_->publish_state(this->waveform_channel_);
  this->high_frequency_loop_.start();
}

void BL0906SPI::on_shutdown() {
  this->high_frequency_loop_.stop();
  this->save_energy_();
  if (this->reset_pin_ != nullptr)
    this->reset_pin_->digital_write(false);
}

bool BL0906SPI::read_register_(uint8_t address, uint32_t &value) {
  uint8_t frame[6] = {BL0906_SPI_READ, address, 0x00, 0x00, 0x00, 0x00};
  this->enable();
  this->transfer_array(frame, sizeof(frame));
  this->disable();

  const uint8_t checksum = static_cast<uint8_t>(~static_cast<uint8_t>(
      BL0906_SPI_READ + address + frame[2] + frame[3] + frame[4]));
  // BL0906 releases MISO at the end of the 48th clock. With ESP32-C3 hardware
  // SPI the checksum LSB is consequently read as the idle-high level on the
  // tested production board. Validate the remaining seven checksum bits; all
  // 24 data bits are sampled before that release point.
  if ((checksum & 0xFEU) != (frame[5] & 0xFEU)) {
    this->spi_errors_++;
    this->consecutive_good_frames_ = 0;
    this->consecutive_bad_frames_++;
    if (this->consecutive_bad_frames_ == 10) {
      ESP_LOGW(TAG, "Repeated SPI checksum failures; check SEL, /CS, SCLK, MOSI and MISO wiring");
      this->status_set_warning();
    }
    return false;
  }

  this->consecutive_bad_frames_ = 0;
  if (this->consecutive_good_frames_ < 50)
    this->consecutive_good_frames_++;
  if (this->consecutive_good_frames_ == 50)
    this->status_clear_warning();

  value = (static_cast<uint32_t>(frame[2]) << 16) | (static_cast<uint32_t>(frame[3]) << 8) | frame[4];
  return true;
}

bool BL0906SPI::read_wave_register_(uint8_t address, int32_t &value, uint32_t &timestamp_us) {
  const uint32_t before = micros();
  uint32_t raw;
  if (!this->read_register_(address, raw))
    return false;
  const uint32_t after = micros();
  timestamp_us = before + static_cast<uint32_t>(after - before) / 2;
  value = sign_extend_24_(raw);
  return true;
}

void BL0906SPI::loop() {
  const uint32_t now = micros();
  if (!time_after_or_equal_(now, this->next_sample_us_))
    return;

  // Do not run a burst after Wi-Fi/API work delayed the main loop. A burst
  // would destroy sample timing and can starve the watchdog.
  if (static_cast<uint32_t>(now - this->next_sample_us_) > this->sample_interval_us_ * 4)
    this->next_sample_us_ = now;
  this->next_sample_us_ += this->sample_interval_us_;

  if (this->acquire_sample_()) {
    this->acquired_since_publish_++;
    this->process_pending_samples_();
  }
}

bool BL0906SPI::acquire_sample_() {
  BL0906WaveSample sample;
  sample.sequence = this->write_sequence_;

  if (!this->read_wave_register_(BL0906_V_WAVE, sample.voltage, sample.voltage_time_us))
    return false;
  for (size_t channel = 0; channel < BL0906_CHANNEL_COUNT; channel++) {
    if (!this->read_wave_register_(BL0906_I_WAVE[channel], sample.current[channel],
                                   sample.current_time_us[channel]))
      return false;
  }

  this->wave_buffer_[this->write_sequence_ % BL0906_WAVE_BUFFER_SIZE] = sample;
  this->write_sequence_++;
  return true;
}

bool BL0906SPI::interpolate_voltage_(uint32_t target_time_us, double &voltage) const {
  const uint64_t oldest = this->write_sequence_ > BL0906_WAVE_BUFFER_SIZE
                              ? this->write_sequence_ - BL0906_WAVE_BUFFER_SIZE
                              : 0;
  if (this->write_sequence_ < 2)
    return false;

  // Processing normally asks for a voltage only a few lookahead frames behind
  // the newest sample. Search backwards so increasing the capture ring does
  // not turn every real-time interpolation into a full-buffer scan.
  for (uint64_t right_sequence = this->write_sequence_ - 1; right_sequence > oldest;
       right_sequence--) {
    const uint64_t left_sequence = right_sequence - 1;
    const auto &left = this->wave_buffer_[left_sequence % BL0906_WAVE_BUFFER_SIZE];
    const auto &right = this->wave_buffer_[right_sequence % BL0906_WAVE_BUFFER_SIZE];
    if (left.sequence != left_sequence || right.sequence != right_sequence)
      continue;
    if (time_after_or_equal_(target_time_us, left.voltage_time_us) &&
        time_after_or_equal_(right.voltage_time_us, target_time_us)) {
      const uint32_t span = right.voltage_time_us - left.voltage_time_us;
      if (span == 0) {
        voltage = left.voltage;
        return true;
      }
      const double ratio = static_cast<double>(target_time_us - left.voltage_time_us) / span;
      voltage = static_cast<double>(left.voltage) +
                (static_cast<double>(right.voltage) - static_cast<double>(left.voltage)) * ratio;
      return true;
    }
  }
  return false;
}

void BL0906SPI::process_pending_samples_() {
  if (!this->processing_started_) {
    if (this->write_sequence_ <= static_cast<uint64_t>(this->lookahead_frames_) * 2)
      return;
    this->next_process_sequence_ = this->lookahead_frames_;
    this->processing_started_ = true;
  }

  const uint64_t oldest = this->write_sequence_ > BL0906_WAVE_BUFFER_SIZE
                              ? this->write_sequence_ - BL0906_WAVE_BUFFER_SIZE
                              : 0;
  if (this->next_process_sequence_ < oldest + this->lookahead_frames_)
    this->next_process_sequence_ = oldest + this->lookahead_frames_;

  // Normally one input sample produces one processed sample. The limit of two
  // lets the pipeline catch up without monopolising the ESPHome loop.
  uint8_t processed = 0;
  while (this->next_process_sequence_ + this->lookahead_frames_ < this->write_sequence_ && processed < 2) {
    const auto &sample = this->wave_buffer_[this->next_process_sequence_ % BL0906_WAVE_BUFFER_SIZE];
    if (sample.sequence != this->next_process_sequence_) {
      this->next_process_sequence_++;
      continue;
    }

    bool ready = true;
    for (size_t channel = 0; channel < BL0906_CHANNEL_COUNT; channel++) {
      const int32_t shift_us = static_cast<int32_t>(std::lround(
          this->phase_offsets_deg_[channel] * 1000000.0 / (360.0 * this->frequency_hz_)));
      const uint32_t target_time = sample.current_time_us[channel] + shift_us;
      double ignored;
      if (!this->interpolate_voltage_(target_time, ignored)) {
        ready = false;
        break;
      }
    }
    if (!ready)
      return;

    this->process_sample_(sample);
    this->next_process_sequence_++;
    processed++;
  }
}

void BL0906SPI::process_sample_(const BL0906WaveSample &sample) {
  const double voltage = sample.voltage;
  this->voltage_square_sum_ += voltage * voltage;

  for (size_t channel = 0; channel < BL0906_CHANNEL_COUNT; channel++) {
    const int32_t shift_us = static_cast<int32_t>(std::lround(
        this->phase_offsets_deg_[channel] * 1000000.0 / (360.0 * this->frequency_hz_)));
    const uint32_t target_time = sample.current_time_us[channel] + shift_us;
    double shifted_voltage;
    if (!this->interpolate_voltage_(target_time, shifted_voltage))
      return;

    const double current = sample.current[channel];
    const double raw_product = current * shifted_voltage;
    const double power_w = raw_product * this->power_calibration_[channel];
    this->current_square_sum_[channel] += current * current;
    this->product_sum_[channel] += raw_product;

    if (this->have_previous_power_[channel]) {
      const uint32_t elapsed_us = sample.current_time_us[channel] - this->previous_power_time_us_[channel];
      if (elapsed_us > 0 && elapsed_us < 100000) {
        // Consumption energy is total-increasing. Negative corrected power is
        // treated as export and is not added to this counter.
        const double previous = std::max(0.0, this->previous_power_w_[channel]);
        const double current_power = std::max(0.0, power_w);
        this->energy_kwh_[channel] +=
            static_cast<float>((previous + current_power) * 0.5 * elapsed_us / 3.6e12);
      }
    }
    this->previous_power_w_[channel] = power_w;
    this->previous_power_time_us_[channel] = sample.current_time_us[channel];
    this->have_previous_power_[channel] = true;
  }
  this->processed_sample_count_++;
}

void BL0906SPI::read_frequency_() {
  uint32_t period;
  if (!this->read_register_(BL0906_PERIOD, period))
    return;
  period &= 0x000FFFFFUL;
  if (period == 0)
    return;
  const float frequency = 10000000.0f / period;
  if (frequency >= 40.0f && frequency <= 70.0f)
    this->frequency_hz_ = frequency;
}

void BL0906SPI::read_temperature_() {
  if (this->temperature_sensor_ == nullptr)
    return;
  uint32_t raw;
  if (!this->read_register_(BL0906_TEMPERATURE, raw))
    return;
  const int32_t signed_raw = sign_extend_24_(raw);
  const float temperature = (static_cast<float>(signed_raw) - 64.0f) * 12.5f / 59.0f - 40.0f;
  if (temperature < -40.0f || temperature > 125.0f) {
    ESP_LOGW(TAG, "Ignoring implausible BL0906 temperature %.1f C", temperature);
    return;
  }
  this->temperature_sensor_->publish_state(temperature);
}

void BL0906SPI::set_waveform_channel(uint8_t channel) {
  if (channel < BL0906_CHANNEL_COUNT)
    this->waveform_channel_ = channel;
}

std::string BL0906SPI::format_waveform_(
    float period_ms, const std::array<double, BL0906_WAVEFORM_POINTS> &values,
    uint8_t decimals) const {
  std::string output = str_sprintf("%.3f|", period_ms);
  for (size_t index = 0; index < values.size(); index++) {
    if (index != 0)
      output.push_back(',');
    if (decimals == 3)
      output += str_sprintf("%.3f", values[index]);
    else
      output += str_sprintf("%.1f", values[index]);
  }
  return output;
}

void BL0906SPI::capture_waveform() {
  size_t configured_periods = 0;
  while (configured_periods < BL0906_WAVEFORM_MAX_PERIODS &&
         this->waveform_voltage_sensors_[configured_periods] != nullptr &&
         this->waveform_current_sensors_[configured_periods] != nullptr &&
         this->waveform_power_sensors_[configured_periods] != nullptr)
    configured_periods++;
  if (configured_periods == 0 || this->write_sequence_ < 4) {
    ESP_LOGW(TAG, "Waveform capture is not configured or the sample buffer is not ready");
    return;
  }

  auto &points = this->waveform_capture_points_;
  size_t point_count = 0;
  const uint64_t oldest = this->write_sequence_ > BL0906_WAVE_BUFFER_SIZE
                              ? this->write_sequence_ - BL0906_WAVE_BUFFER_SIZE
                              : 0;
  const int32_t shift_us = static_cast<int32_t>(std::lround(
      this->phase_offsets_deg_[this->waveform_channel_] * 1000000.0 / (360.0 * this->frequency_hz_)));
  for (uint64_t sequence = oldest; sequence < this->write_sequence_ && point_count < points.size(); sequence++) {
    const auto &sample = this->wave_buffer_[sequence % BL0906_WAVE_BUFFER_SIZE];
    if (sample.sequence != sequence)
      continue;
    const uint32_t target_time = sample.current_time_us[this->waveform_channel_] + shift_us;
    double shifted_voltage;
    if (!this->interpolate_voltage_(target_time, shifted_voltage))
      continue;
    points[point_count++] = {sample.current_time_us[this->waveform_channel_], shifted_voltage,
                             static_cast<double>(sample.current[this->waveform_channel_])};
  }

  if (point_count < 4) {
    ESP_LOGW(TAG, "No complete waveform is available in the sample buffer");
    return;
  }

  std::array<size_t, 16> crossings{};
  size_t crossing_count = 0;
  for (size_t index = 1; index < point_count; index++) {
    if (points[index - 1].voltage < 0.0 && points[index].voltage >= 0.0 &&
        crossing_count < crossings.size())
      crossings[crossing_count++] = index;
  }
  if (crossing_count < 2) {
    ESP_LOGW(TAG, "No complete positive-going voltage cycle is available");
    return;
  }

  auto crossing = [&](size_t right) -> BL0906WaveformPoint {
    const BL0906WaveformPoint &left_point = points[right - 1];
    const BL0906WaveformPoint &right_point = points[right];
    const double denominator = right_point.voltage - left_point.voltage;
    const double ratio = denominator == 0.0 ? 0.0 : -left_point.voltage / denominator;
    BL0906WaveformPoint result;
    result.time_us = left_point.time_us + static_cast<uint32_t>(
        ratio * static_cast<double>(right_point.time_us - left_point.time_us));
    result.voltage = 0.0;
    result.current = left_point.current + ratio * (right_point.current - left_point.current);
    return result;
  };

  auto &candidates = this->waveform_period_candidates_;
  std::array<bool, 15> candidate_valid{};
  const size_t candidate_count = crossing_count - 1;
  for (size_t candidate = 0; candidate < candidate_count; candidate++) {
    const size_t candidate_start = crossings[candidate];
    const size_t candidate_end = crossings[candidate + 1];
    const BL0906WaveformPoint candidate_start_point = crossing(candidate_start);
    const BL0906WaveformPoint candidate_end_point = crossing(candidate_end);
    const uint32_t duration_us = candidate_end_point.time_us - candidate_start_point.time_us;
    if (duration_us < 12000 || duration_us > 30000)
      continue;
    // A Wi-Fi/API service pass can occasionally delay the high-frequency loop
    // by several milliseconds. Real timestamps make interpolation across that
    // delay safe, but never bridge half a mains cycle: that could hide a lost
    // zero crossing and turn an incomplete cycle into a plausible one.
    const uint32_t maximum_gap_us = duration_us / 2;
    bool has_gap = false;
    for (size_t index = candidate_start + 1; index <= candidate_end; index++) {
      if (points[index].time_us - points[index - 1].time_us > maximum_gap_us) {
        has_gap = true;
        break;
      }
    }
    if (!has_gap) {
      candidates[candidate] = {candidate_start, candidate_end, candidate_start_point,
                               candidate_end_point, duration_us};
      candidate_valid[candidate] = true;
    }
  }

  // Prefer the newest longest consecutive run. If an API/Wi-Fi delay damaged
  // the latest cycle, an older complete five-cycle run is still usable.
  size_t current_length = 0;
  size_t best_start = 0;
  size_t best_length = 0;
  for (size_t candidate = 0; candidate < candidate_count; candidate++) {
    if (!candidate_valid[candidate]) {
      current_length = 0;
      continue;
    }
    current_length++;
    const size_t usable_length = std::min(current_length, configured_periods);
    const size_t usable_start = candidate + 1 - usable_length;
    if (usable_length > best_length || (usable_length == best_length && usable_start > best_start)) {
      best_start = usable_start;
      best_length = usable_length;
    }
  }
  if (best_length == 0) {
    ESP_LOGW(TAG, "No complete waveform without sample gaps is available");
    return;
  }

  std::array<std::string, BL0906_WAVEFORM_MAX_PERIODS> voltage_states{};
  std::array<std::string, BL0906_WAVEFORM_MAX_PERIODS> current_states{};
  std::array<std::string, BL0906_WAVEFORM_MAX_PERIODS> power_states{};
  for (size_t period_index = 0; period_index < best_length; period_index++) {
    const BL0906WaveformPeriodWindow &window = candidates[best_start + period_index];
    std::array<double, BL0906_WAVEFORM_POINTS> voltage{};
    std::array<double, BL0906_WAVEFORM_POINTS> current{};
    std::array<double, BL0906_WAVEFORM_POINTS> power{};
    size_t segment = window.first_sample;
    for (size_t output_index = 0; output_index < BL0906_WAVEFORM_POINTS; output_index++) {
      const uint32_t target_elapsed = static_cast<uint32_t>(
          static_cast<uint64_t>(window.duration_us) * output_index / (BL0906_WAVEFORM_POINTS - 1));
      const uint32_t target_time = window.start.time_us + target_elapsed;
      double raw_voltage = 0.0;
      double raw_current = window.start.current;
      if (output_index == BL0906_WAVEFORM_POINTS - 1) {
        raw_current = window.end.current;
      } else if (output_index != 0) {
        while (segment < window.last_sample && time_after_or_equal_(target_time, points[segment].time_us))
          segment++;
        const BL0906WaveformPoint &right_point = points[segment];
        const BL0906WaveformPoint &left_point = points[segment - 1];
        const uint32_t span = right_point.time_us - left_point.time_us;
        const double ratio = span == 0 ? 0.0 : static_cast<double>(target_time - left_point.time_us) / span;
        raw_voltage = left_point.voltage + ratio * (right_point.voltage - left_point.voltage);
        raw_current = left_point.current + ratio * (right_point.current - left_point.current);
      }
      voltage[output_index] = raw_voltage * this->voltage_calibration_;
      current[output_index] = raw_current * this->current_calibration_[this->waveform_channel_];
      // The waveform is deliberately the physical instantaneous product U * I.
      power[output_index] = voltage[output_index] * current[output_index];
    }

    const float period_ms = window.duration_us / 1000.0f;
    voltage_states[period_index] = this->format_waveform_(period_ms, voltage, 1);
    current_states[period_index] = this->format_waveform_(period_ms, current, 3);
    power_states[period_index] = this->format_waveform_(period_ms, power, 1);
    if (voltage_states[period_index].size() > 255 || current_states[period_index].size() > 255 ||
        power_states[period_index].size() > 255) {
      ESP_LOGW(TAG, "Waveform period payload exceeds the Home Assistant state limit");
      return;
    }
  }

  // Publish the completion marker last. HA templates trigger on it and see an
  // atomic snapshot of all period parts rather than a mixture of two captures.
  for (size_t period_index = 0; period_index < configured_periods; period_index++) {
    const bool present = period_index < best_length;
    this->waveform_voltage_sensors_[period_index]->publish_state(
        present ? voltage_states[period_index] : "0|");
    this->waveform_current_sensors_[period_index]->publish_state(
        present ? current_states[period_index] : "0|");
    this->waveform_power_sensors_[period_index]->publish_state(
        present ? power_states[period_index] : "0|");
  }
  if (this->waveform_capture_id_sensor_ != nullptr) {
    this->waveform_capture_id_sensor_->publish_state(
        str_sprintf("%lu-%lu", static_cast<unsigned long>(millis()),
                    static_cast<unsigned long>(++this->waveform_capture_sequence_)));
  }
  ESP_LOGI(TAG, "Captured %u complete waveform periods for channel %u",
           static_cast<unsigned>(best_length), static_cast<unsigned>(this->waveform_channel_ + 1));
}

void BL0906SPI::update() {
  this->read_frequency_();
  this->read_temperature_();

  const uint32_t now_ms = millis();
  const uint32_t elapsed_ms = now_ms - this->publish_window_start_ms_;
  if (this->actual_sample_rate_sensor_ != nullptr && elapsed_ms > 0) {
    this->actual_sample_rate_sensor_->publish_state(
        static_cast<float>(this->acquired_since_publish_) * 1000.0f / elapsed_ms);
  }
  this->publish_window_start_ms_ = now_ms;
  this->acquired_since_publish_ = 0;

  if (this->frequency_sensor_ != nullptr)
    this->frequency_sensor_->publish_state(this->frequency_hz_);
  if (this->spi_errors_sensor_ != nullptr)
    this->spi_errors_sensor_->publish_state(this->spi_errors_);

  if (this->processed_sample_count_ == 0) {
    ESP_LOGW(TAG, "No valid waveform samples in this update interval");
    return;
  }

  const double divisor = this->processed_sample_count_;
  const double raw_voltage_rms = std::sqrt(this->voltage_square_sum_ / divisor);
  const double voltage_rms = raw_voltage_rms * this->voltage_calibration_;
  if (this->raw_voltage_rms_sensor_ != nullptr)
    this->raw_voltage_rms_sensor_->publish_state(raw_voltage_rms);
  if (this->voltage_sensor_ != nullptr)
    this->voltage_sensor_->publish_state(voltage_rms);

  double total_power = 0.0;
  double total_apparent_power = 0.0;
  float total_energy = 0.0f;
  for (size_t channel = 0; channel < BL0906_CHANNEL_COUNT; channel++) {
    const double raw_current_rms = std::sqrt(this->current_square_sum_[channel] / divisor);
    const double raw_power = this->product_sum_[channel] / divisor;
    const double current = raw_current_rms * this->current_calibration_[channel];
    const double power = raw_power * this->power_calibration_[channel];
    const double apparent_power = voltage_rms * current;
    const double power_factor = apparent_power >= 1.0
                                    ? std::max(-1.0, std::min(1.0, power / apparent_power))
                                    : NAN;
    const double reactive_power = std::sqrt(std::max(0.0, apparent_power * apparent_power - power * power));
    const double phase_angle = std::isfinite(power_factor) ? std::acos(power_factor) * 180.0 / M_PI : NAN;
    total_power += power;
    total_apparent_power += apparent_power;
    total_energy += this->energy_kwh_[channel];

    if (this->raw_current_rms_sensors_[channel] != nullptr)
      this->raw_current_rms_sensors_[channel]->publish_state(raw_current_rms);
    if (this->raw_power_sensors_[channel] != nullptr)
      this->raw_power_sensors_[channel]->publish_state(raw_power);
    if (this->current_sensors_[channel] != nullptr)
      this->current_sensors_[channel]->publish_state(current);
    if (this->power_sensors_[channel] != nullptr)
      this->power_sensors_[channel]->publish_state(power);
    if (this->energy_sensors_[channel] != nullptr)
      this->energy_sensors_[channel]->publish_state(this->energy_kwh_[channel]);
    if (this->apparent_power_sensors_[channel] != nullptr)
      this->apparent_power_sensors_[channel]->publish_state(apparent_power);
    if (this->power_factor_sensors_[channel] != nullptr)
      this->power_factor_sensors_[channel]->publish_state(power_factor * 100.0);
    if (this->reactive_power_sensors_[channel] != nullptr)
      this->reactive_power_sensors_[channel]->publish_state(reactive_power);
    if (this->phase_angle_sensors_[channel] != nullptr)
      this->phase_angle_sensors_[channel]->publish_state(phase_angle);
  }

  if (this->total_power_sensor_ != nullptr)
    this->total_power_sensor_->publish_state(total_power);
  if (this->total_energy_sensor_ != nullptr)
    this->total_energy_sensor_->publish_state(total_energy);
  if (this->total_apparent_power_sensor_ != nullptr)
    this->total_apparent_power_sensor_->publish_state(total_apparent_power);
  if (this->total_power_factor_sensor_ != nullptr) {
    const double total_power_factor = total_apparent_power >= 1.0
                                          ? std::max(-1.0, std::min(1.0, total_power / total_apparent_power))
                                          : NAN;
    this->total_power_factor_sensor_->publish_state(total_power_factor * 100.0);
  }

  this->voltage_square_sum_ = 0.0;
  this->current_square_sum_.fill(0.0);
  this->product_sum_.fill(0.0);
  this->processed_sample_count_ = 0;
}

void BL0906SPI::save_energy_() {
  if (!this->preference_ready_)
    return;
  BL0906EnergyRestoreState state;
  state.energy_kwh = this->energy_kwh_;
  this->energy_preference_.save(&state);
}

void BL0906SPI::dump_config() {
  ESP_LOGCONFIG(TAG, "BL0906 SPI waveform meter:");
  LOG_PIN("  SEL pin: ", this->select_pin_);
  LOG_PIN("  Reset pin: ", this->reset_pin_);
  LOG_PIN("  CS pin: ", this->cs_);
  ESP_LOGCONFIG(TAG, "  SPI mode: 1");
  ESP_LOGCONFIG(TAG, "  Requested sample rate: %.0f Hz", this->requested_sample_rate_hz_);
  ESP_LOGCONFIG(TAG, "  Phase offsets: %.1f, %.1f, %.1f, %.1f, %.1f, %.1f degrees",
                this->phase_offsets_deg_[0], this->phase_offsets_deg_[1], this->phase_offsets_deg_[2],
                this->phase_offsets_deg_[3], this->phase_offsets_deg_[4], this->phase_offsets_deg_[5]);
  LOG_SENSOR("  ", "Voltage", this->voltage_sensor_);
  LOG_SENSOR("  ", "Frequency", this->frequency_sensor_);
  LOG_SENSOR("  ", "Temperature", this->temperature_sensor_);
  LOG_SENSOR("  ", "Total Power", this->total_power_sensor_);
  LOG_SENSOR("  ", "Total Energy", this->total_energy_sensor_);
  LOG_SENSOR("  ", "Total Apparent Power", this->total_apparent_power_sensor_);
  LOG_SENSOR("  ", "Total Power Factor", this->total_power_factor_sensor_);
  LOG_SELECT("  ", "Waveform Channel", this->waveform_channel_select_);
  LOG_BUTTON("  ", "Capture Waveform", this->waveform_capture_button_);
  for (size_t period = 0; period < BL0906_WAVEFORM_MAX_PERIODS; period++) {
    LOG_TEXT_SENSOR("  ", "Waveform Voltage Part", this->waveform_voltage_sensors_[period]);
    LOG_TEXT_SENSOR("  ", "Waveform Current Part", this->waveform_current_sensors_[period]);
    LOG_TEXT_SENSOR("  ", "Waveform Power Part", this->waveform_power_sensors_[period]);
  }
  LOG_TEXT_SENSOR("  ", "Waveform Capture ID", this->waveform_capture_id_sensor_);
  for (size_t channel = 0; channel < BL0906_CHANNEL_COUNT; channel++) {
    ESP_LOGCONFIG(TAG, "  Channel %u:", static_cast<unsigned>(channel + 1));
    LOG_SENSOR("    ", "Current", this->current_sensors_[channel]);
    LOG_SENSOR("    ", "Corrected Power", this->power_sensors_[channel]);
    LOG_SENSOR("    ", "Corrected Energy", this->energy_sensors_[channel]);
    LOG_SENSOR("    ", "Apparent Power", this->apparent_power_sensors_[channel]);
    LOG_SENSOR("    ", "Power Factor", this->power_factor_sensors_[channel]);
    LOG_SENSOR("    ", "Reactive Power", this->reactive_power_sensors_[channel]);
    LOG_SENSOR("    ", "Phase Angle", this->phase_angle_sensors_[channel]);
  }
}

}  // namespace bl0906_spi
}  // namespace esphome
