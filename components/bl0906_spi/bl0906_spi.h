#pragma once

#include <array>
#include <cstdint>
#include <string>

#include "esphome/components/button/button.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/select/select.h"
#include "esphome/components/spi/spi.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "esphome/core/preferences.h"

namespace esphome {
namespace bl0906_spi {

static constexpr size_t BL0906_CHANNEL_COUNT = 6;
static constexpr size_t BL0906_WAVE_BUFFER_SIZE = 128;

struct BL0906WaveSample {
  uint64_t sequence{UINT64_MAX};
  uint32_t voltage_time_us{0};
  int32_t voltage{0};
  std::array<uint32_t, BL0906_CHANNEL_COUNT> current_time_us{};
  std::array<int32_t, BL0906_CHANNEL_COUNT> current{};
};

struct BL0906EnergyRestoreState {
  std::array<float, BL0906_CHANNEL_COUNT> energy_kwh{};
};

class BL0906SPI;

class BL0906WaveformChannelSelect : public select::Select {
 public:
  void set_parent(BL0906SPI *parent) { this->parent_ = parent; }

 protected:
  void control(size_t index) override;
  BL0906SPI *parent_{nullptr};
};

class BL0906WaveformCaptureButton : public button::Button {
 public:
  void set_parent(BL0906SPI *parent) { this->parent_ = parent; }

 protected:
  void press_action() override;
  BL0906SPI *parent_{nullptr};
};

class BL0906SPI : public PollingComponent,
                  public spi::SPIDevice<spi::BIT_ORDER_MSB_FIRST, spi::CLOCK_POLARITY_LOW,
                                        spi::CLOCK_PHASE_TRAILING, spi::DATA_RATE_1MHZ> {
 public:
  void setup() override;
  void loop() override;
  void update() override;
  void dump_config() override;
  void on_shutdown() override;

  float get_setup_priority() const override { return setup_priority::DATA; }

  void set_select_pin(GPIOPin *pin) { this->select_pin_ = pin; }
  void set_reset_pin(GPIOPin *pin) { this->reset_pin_ = pin; }
  void set_sample_rate(float sample_rate) { this->requested_sample_rate_hz_ = sample_rate; }
  void set_restore_energy(bool restore) { this->restore_energy_ = restore; }
  void set_voltage_calibration(float calibration) { this->voltage_calibration_ = calibration; }
  void set_phase_offset(uint8_t channel, float offset) { this->phase_offsets_deg_[channel] = offset; }
  void set_current_calibration(uint8_t channel, float calibration) {
    this->current_calibration_[channel] = calibration;
  }
  void set_power_calibration(uint8_t channel, float calibration) {
    this->power_calibration_[channel] = calibration;
  }
  void set_waveform_channel(uint8_t channel);
  void capture_waveform();

  void set_voltage_sensor(sensor::Sensor *sensor) { this->voltage_sensor_ = sensor; }
  void set_frequency_sensor(sensor::Sensor *sensor) { this->frequency_sensor_ = sensor; }
  void set_total_power_sensor(sensor::Sensor *sensor) { this->total_power_sensor_ = sensor; }
  void set_total_energy_sensor(sensor::Sensor *sensor) { this->total_energy_sensor_ = sensor; }
  void set_total_apparent_power_sensor(sensor::Sensor *sensor) { this->total_apparent_power_sensor_ = sensor; }
  void set_total_power_factor_sensor(sensor::Sensor *sensor) { this->total_power_factor_sensor_ = sensor; }
  void set_actual_sample_rate_sensor(sensor::Sensor *sensor) { this->actual_sample_rate_sensor_ = sensor; }
  void set_spi_errors_sensor(sensor::Sensor *sensor) { this->spi_errors_sensor_ = sensor; }
  void set_raw_voltage_rms_sensor(sensor::Sensor *sensor) { this->raw_voltage_rms_sensor_ = sensor; }
  void set_temperature_sensor(sensor::Sensor *sensor) { this->temperature_sensor_ = sensor; }
  void set_waveform_channel_select(BL0906WaveformChannelSelect *select) {
    this->waveform_channel_select_ = select;
  }
  void set_waveform_capture_button(BL0906WaveformCaptureButton *button) {
    this->waveform_capture_button_ = button;
  }
  void set_waveform_voltage_sensor(text_sensor::TextSensor *sensor) { this->waveform_voltage_sensor_ = sensor; }
  void set_waveform_current_sensor(text_sensor::TextSensor *sensor) { this->waveform_current_sensor_ = sensor; }
  void set_waveform_power_sensor(text_sensor::TextSensor *sensor) { this->waveform_power_sensor_ = sensor; }

  void set_current_sensor(uint8_t channel, sensor::Sensor *sensor) { this->current_sensors_[channel] = sensor; }
  void set_power_sensor(uint8_t channel, sensor::Sensor *sensor) { this->power_sensors_[channel] = sensor; }
  void set_energy_sensor(uint8_t channel, sensor::Sensor *sensor) { this->energy_sensors_[channel] = sensor; }
  void set_raw_current_rms_sensor(uint8_t channel, sensor::Sensor *sensor) {
    this->raw_current_rms_sensors_[channel] = sensor;
  }
  void set_raw_power_sensor(uint8_t channel, sensor::Sensor *sensor) {
    this->raw_power_sensors_[channel] = sensor;
  }
  void set_apparent_power_sensor(uint8_t channel, sensor::Sensor *sensor) {
    this->apparent_power_sensors_[channel] = sensor;
  }
  void set_power_factor_sensor(uint8_t channel, sensor::Sensor *sensor) {
    this->power_factor_sensors_[channel] = sensor;
  }
  void set_reactive_power_sensor(uint8_t channel, sensor::Sensor *sensor) {
    this->reactive_power_sensors_[channel] = sensor;
  }
  void set_phase_angle_sensor(uint8_t channel, sensor::Sensor *sensor) {
    this->phase_angle_sensors_[channel] = sensor;
  }

 protected:
  bool read_register_(uint8_t address, uint32_t &value);
  bool read_wave_register_(uint8_t address, int32_t &value, uint32_t &timestamp_us);
  bool acquire_sample_();
  void process_pending_samples_();
  bool interpolate_voltage_(uint32_t target_time_us, double &voltage) const;
  void process_sample_(const BL0906WaveSample &sample);
  void read_frequency_();
  void read_temperature_();
  void save_energy_();
  std::string format_waveform_(float period_ms, const std::array<double, 21> &values, uint8_t decimals) const;

  static int32_t sign_extend_24_(uint32_t value);
  static bool time_after_or_equal_(uint32_t a, uint32_t b) { return static_cast<int32_t>(a - b) >= 0; }

  GPIOPin *select_pin_{nullptr};
  GPIOPin *reset_pin_{nullptr};
  HighFrequencyLoopRequester high_frequency_loop_;

  float requested_sample_rate_hz_{1000.0f};
  uint32_t sample_interval_us_{1000};
  uint32_t next_sample_us_{0};
  uint16_t lookahead_frames_{16};
  float frequency_hz_{50.0f};

  std::array<BL0906WaveSample, BL0906_WAVE_BUFFER_SIZE> wave_buffer_{};
  uint64_t write_sequence_{0};
  uint64_t next_process_sequence_{0};
  bool processing_started_{false};

  std::array<double, BL0906_CHANNEL_COUNT> phase_offsets_deg_{{120.0, 0.0, -120.0, 0.0, 0.0, 0.0}};
  float voltage_calibration_{8.334599604923265e-05f};
  std::array<float, BL0906_CHANNEL_COUNT> current_calibration_{{
      1.6706643822577576e-05f, 1.6706643822577576e-05f, 1.6706643822577576e-05f,
      1.6706643822577576e-05f, 1.6706643822577576e-05f, 1.6706643822577576e-05f,
  }};
  std::array<float, BL0906_CHANNEL_COUNT> power_calibration_{{
      1.4234677637430922e-09f, 1.4234677637430922e-09f, 1.4234677637430922e-09f,
      1.4234677637430922e-09f, 1.4234677637430922e-09f, 1.4234677637430922e-09f,
  }};

  double voltage_square_sum_{0.0};
  std::array<double, BL0906_CHANNEL_COUNT> current_square_sum_{};
  std::array<double, BL0906_CHANNEL_COUNT> product_sum_{};
  uint32_t processed_sample_count_{0};

  std::array<double, BL0906_CHANNEL_COUNT> previous_power_w_{};
  std::array<uint32_t, BL0906_CHANNEL_COUNT> previous_power_time_us_{};
  std::array<bool, BL0906_CHANNEL_COUNT> have_previous_power_{};
  std::array<float, BL0906_CHANNEL_COUNT> energy_kwh_{};

  uint32_t acquired_since_publish_{0};
  uint32_t publish_window_start_ms_{0};
  uint32_t spi_errors_{0};
  uint16_t consecutive_good_frames_{0};
  uint16_t consecutive_bad_frames_{0};

  bool restore_energy_{true};
  bool preference_ready_{false};
  ESPPreferenceObject energy_preference_;

  sensor::Sensor *voltage_sensor_{nullptr};
  sensor::Sensor *frequency_sensor_{nullptr};
  sensor::Sensor *total_power_sensor_{nullptr};
  sensor::Sensor *total_energy_sensor_{nullptr};
  sensor::Sensor *total_apparent_power_sensor_{nullptr};
  sensor::Sensor *total_power_factor_sensor_{nullptr};
  sensor::Sensor *actual_sample_rate_sensor_{nullptr};
  sensor::Sensor *spi_errors_sensor_{nullptr};
  sensor::Sensor *raw_voltage_rms_sensor_{nullptr};
  sensor::Sensor *temperature_sensor_{nullptr};
  BL0906WaveformChannelSelect *waveform_channel_select_{nullptr};
  BL0906WaveformCaptureButton *waveform_capture_button_{nullptr};
  text_sensor::TextSensor *waveform_voltage_sensor_{nullptr};
  text_sensor::TextSensor *waveform_current_sensor_{nullptr};
  text_sensor::TextSensor *waveform_power_sensor_{nullptr};
  uint8_t waveform_channel_{0};
  std::array<sensor::Sensor *, BL0906_CHANNEL_COUNT> current_sensors_{};
  std::array<sensor::Sensor *, BL0906_CHANNEL_COUNT> power_sensors_{};
  std::array<sensor::Sensor *, BL0906_CHANNEL_COUNT> energy_sensors_{};
  std::array<sensor::Sensor *, BL0906_CHANNEL_COUNT> raw_current_rms_sensors_{};
  std::array<sensor::Sensor *, BL0906_CHANNEL_COUNT> raw_power_sensors_{};
  std::array<sensor::Sensor *, BL0906_CHANNEL_COUNT> apparent_power_sensors_{};
  std::array<sensor::Sensor *, BL0906_CHANNEL_COUNT> power_factor_sensors_{};
  std::array<sensor::Sensor *, BL0906_CHANNEL_COUNT> reactive_power_sensors_{};
  std::array<sensor::Sensor *, BL0906_CHANNEL_COUNT> phase_angle_sensors_{};
};

}  // namespace bl0906_spi
}  // namespace esphome
