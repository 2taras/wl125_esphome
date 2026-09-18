#include "bl0906_spi.h"

#include <algorithm>
#include <cmath>

#include "esphome/core/log.h"

namespace esphome {
namespace bl0906_spi {

static const char *const TAG = "bl0906_spi";

static constexpr uint8_t BL0906_SPI_READ = 0x82;
static constexpr uint8_t BL0906_I_WAVE[BL0906_CHANNEL_COUNT] = {0x02, 0x03, 0x04, 0x05, 0x08, 0x09};
static constexpr uint8_t BL0906_V_WAVE = 0x0B;
static constexpr uint8_t BL0906_PERIOD = 0x4E;

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
          key_sensor->get_object_id_hash() ^ 0xB1090601UL);
      BL0906EnergyRestoreState restored;
      if (this->energy_preference_.load(&restored)) {
        this->energy_kwh_ = restored.energy_kwh;
        ESP_LOGI(TAG, "Restored corrected energy counters");
      }
      this->preference_ready_ = true;
      this->set_interval("save_energy", 300000, [this]() { this->save_energy_(); });
    }
  }

  this->publish_window_start_ms_ = millis();
  this->next_sample_us_ = micros() + this->sample_interval_us_;
  this->read_frequency_();
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

  for (uint64_t sequence = oldest; sequence + 1 < this->write_sequence_; sequence++) {
    const auto &left = this->wave_buffer_[sequence % BL0906_WAVE_BUFFER_SIZE];
    const auto &right = this->wave_buffer_[(sequence + 1) % BL0906_WAVE_BUFFER_SIZE];
    if (left.sequence != sequence || right.sequence != sequence + 1)
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

void BL0906SPI::update() {
  this->read_frequency_();

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
  if (this->raw_voltage_rms_sensor_ != nullptr)
    this->raw_voltage_rms_sensor_->publish_state(raw_voltage_rms);
  if (this->voltage_sensor_ != nullptr)
    this->voltage_sensor_->publish_state(raw_voltage_rms * this->voltage_calibration_);

  double total_power = 0.0;
  float total_energy = 0.0f;
  for (size_t channel = 0; channel < BL0906_CHANNEL_COUNT; channel++) {
    const double raw_current_rms = std::sqrt(this->current_square_sum_[channel] / divisor);
    const double raw_power = this->product_sum_[channel] / divisor;
    const double power = raw_power * this->power_calibration_[channel];
    total_power += power;
    total_energy += this->energy_kwh_[channel];

    if (this->raw_current_rms_sensors_[channel] != nullptr)
      this->raw_current_rms_sensors_[channel]->publish_state(raw_current_rms);
    if (this->raw_power_sensors_[channel] != nullptr)
      this->raw_power_sensors_[channel]->publish_state(raw_power);
    if (this->current_sensors_[channel] != nullptr)
      this->current_sensors_[channel]->publish_state(raw_current_rms * this->current_calibration_[channel]);
    if (this->power_sensors_[channel] != nullptr)
      this->power_sensors_[channel]->publish_state(power);
    if (this->energy_sensors_[channel] != nullptr)
      this->energy_sensors_[channel]->publish_state(this->energy_kwh_[channel]);
  }

  if (this->total_power_sensor_ != nullptr)
    this->total_power_sensor_->publish_state(total_power);
  if (this->total_energy_sensor_ != nullptr)
    this->total_energy_sensor_->publish_state(total_energy);

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
  LOG_SENSOR("  ", "Total Power", this->total_power_sensor_);
  LOG_SENSOR("  ", "Total Energy", this->total_energy_sensor_);
  for (size_t channel = 0; channel < BL0906_CHANNEL_COUNT; channel++) {
    ESP_LOGCONFIG(TAG, "  Channel %u:", static_cast<unsigned>(channel + 1));
    LOG_SENSOR("    ", "Current", this->current_sensors_[channel]);
    LOG_SENSOR("    ", "Corrected Power", this->power_sensors_[channel]);
    LOG_SENSOR("    ", "Corrected Energy", this->energy_sensors_[channel]);
  }
}

}  // namespace bl0906_spi
}  // namespace esphome
