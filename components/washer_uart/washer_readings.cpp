#include "washer_readings.h"

#include <cmath>
#include <iterator>

namespace esphome::washer_uart {
namespace {

struct VolumePoint {
  uint16_t code;
  float liters;
};

constexpr VolumePoint VOLUME_POINTS[] = {{2561, 0.0f},   {2557, 0.058f}, {2525, 0.885f}, {2500, 2.132f},
                                         {2480, 3.467f}, {2460, 5.014f}, {2440, 6.773f}, {2430, 7.726f}};
constexpr VolumePoint LAST_POINT = VOLUME_POINTS[std::size(VOLUME_POINTS) - 1];
const Reading UNAVAILABLE{NAN, ReadingBasis::UNAVAILABLE};

bool in_range(uint8_t value, uint8_t first, uint8_t last) { return value >= first && value <= last; }

bool known_water_stage(const WasherStatus &status) {
  const uint8_t step = status.step_code;
  switch (status.program_code) {
    case 0x01:  // Cotton
      return step == 13;
    case 0x04:  // Mix
      return in_range(step, 15, 18) || in_range(step, 23, 24) || in_range(step, 53, 54);
    case 0x08:  // Drum clean
      return in_range(step, 9, 12) || step == 14;
    case 0x35:  // Wash and dry
      return in_range(step, 9, 10) || in_range(step, 14, 15) || step == 21;
    default:
      return false;
  }
}

}  // namespace

Reading water_temperature(const WasherStatus &status) {
  const auto code = status.temperature_code;
  if ((status.state_code != 2 && status.state_code != 3) || status.water_level_code >= 2561 ||
      !known_water_stage(status) || !in_range(code, 35, 174))
    return UNAVAILABLE;

  // Treat the code as a normalized divider reading. The ratio is fitted to
  // temperature measurements, with beta fixed, rather than measured on the board.
  constexpr float FULL_SCALE = 255.0f;
  constexpr float BETA_K = 3368.0f;
  constexpr float REFERENCE_TO_NTC_R25 = 0.3244f;
  const float resistance_ratio = REFERENCE_TO_NTC_R25 * (FULL_SCALE - code) / code;
  const float kelvin = 1.0f / (1.0f / 298.15f + std::log(resistance_ratio) / BETA_K);
  const auto basis = in_range(code, 63, 98) ? ReadingBasis::CALIBRATED_NTC : ReadingBasis::EXTRAPOLATED;
  return {kelvin - 273.15f, basis};
}

Reading water_volume(uint16_t code) {
  if (code < 2300 || code > 2635)
    return UNAVAILABLE;
  if (code >= VOLUME_POINTS[0].code)
    return {0.0f, ReadingBasis::BASELINE};
  if (code < LAST_POINT.code) {
    // Quadratic fit to weighed points from code 2500 to 2430, anchored at 2430.
    const float code_delta = LAST_POINT.code - code;
    return {LAST_POINT.liters + code_delta * (0.09822864f + 0.0002614931f * code_delta),
            ReadingBasis::EXTRAPOLATED};
  }

  for (std::size_t i = 1; i < std::size(VOLUME_POINTS); i++) {
    const auto &upper_code = VOLUME_POINTS[i - 1];
    const auto &lower_code = VOLUME_POINTS[i];
    if (code >= lower_code.code) {
      const float fraction = static_cast<float>(upper_code.code - code) / (upper_code.code - lower_code.code);
      return {upper_code.liters + (lower_code.liters - upper_code.liters) * fraction, ReadingBasis::INTERPOLATED};
    }
  }
  return UNAVAILABLE;
}

float selected_temperature(uint8_t settings) {
  constexpr float TEMPERATURES[] = {NAN, NAN, 20, 30, 40, 60, 90};
  const uint8_t index = settings & 0x0F;
  return index < std::size(TEMPERATURES) ? TEMPERATURES[index] : NAN;
}

const char *reading_basis(ReadingBasis basis) {
  switch (basis) {
    case ReadingBasis::INTERPOLATED: return "По замерам";
    case ReadingBasis::CALIBRATED_NTC: return "Модель NTC по замерам";
    case ReadingBasis::EXTRAPOLATED: return "За пределами замеров";
    case ReadingBasis::BASELINE: return "Ноль калибровки";
    default: return "Нет данных";
  }
}

const char *program_name(uint8_t code) {
  switch (code) {
    case 0x01: return "Хлопок";
    case 0x03: return "Быстрая 15 мин";
    case 0x04: return "Микс";
    case 0x06: return "Шерсть";
    case 0x08: return "Очистка барабана";
    case 0x0A: return "Отжим";
    case 0x0B: return "Полоскание + отжим";
    case 0x0D: return "Детские вещи";
    case 0x11: return "20 °C";
    case 0x17: return "Только сушка";
    case 0x1E: return "Мой режим";
    case 0x34: return "ЭКО 40–60";
    case 0x35: return "Стирка + сушка";
    case 0x37: return "Антибактериальная";
    default: return "Неизвестная программа";
  }
}

const char *state_name(uint8_t code) {
  switch (code) {
    case 0: return "Выключена";
    case 1: return "Выбор программы";
    case 2: return "Запуск или работа";
    case 3: return "Пауза";
    case 4: return "Завершено";
    case 6: return "Отложенный старт";
    default: return "Неизвестное состояние";
  }
}

const char *drying_name(uint8_t settings) {
  switch (settings >> 4) {
    case 1: return "Выключена";
    case 3: return "Авто";
    case 4: return "Солнце с плюсом";
    case 6: return "30 мин";
    case 7: return "60 мин";
    case 8: return "90 мин";
    case 9: return "120 мин";
    case 10: return "180 мин";
    default: return "Нет данных";
  }
}

const char *cycle_stage(const WasherStatus &status) {
  if (status.state_code != 2 && status.state_code != 3)
    return "—";

  const uint8_t step = status.step_code;
  if (step == 0) return "Запуск";
  switch (status.program_code) {
    case 0x01:
      if (step == 12) return "Налив";
      if (step == 13) return "Стирка";
      break;
    case 0x03:
      if (step == 1) return "Налив";
      if (step == 2) return "Стирка";
      if (step == 3 || step == 6 || step == 16) return "Слив";
      if (step == 4 || step == 14) return "Налив для полоскания";
      if (step == 5 || step == 15) return "Полоскание";
      if (in_range(step, 17, 18)) return "Отжим";
      if (step == 19) return "Завершение";
      break;
    case 0x04:
      if (in_range(step, 15, 17)) return "Налив и замачивание";
      if (step == 18) return "Стирка";
      if (step == 20 || step == 25 || step == 55) return "Слив";
      if (in_range(step, 21, 22) || in_range(step, 26, 27)) return "Промежуточный отжим";
      if (in_range(step, 23, 24) || in_range(step, 53, 54)) return "Полоскание";
      if (in_range(step, 56, 57) || in_range(step, 61, 62)) return "Отжим";
      if (step == 64) return "Сушка";
      if (step == 65) return "Завершение сушки";
      break;
    case 0x08:
      if (in_range(step, 1, 9)) return "Налив";
      if (in_range(step, 10, 14)) return "Очистка барабана";
      if (step == 15) return "Слив";
      if (in_range(step, 16, 17)) return "Отжим";
      if (step == 19) return "Завершение";
      break;
    case 0x35:
      if (step == 4) return "Налив";
      if (in_range(step, 9, 10)) return "Стирка";
      if (step == 11 || step == 16 || step == 23) return "Слив";
      if (in_range(step, 12, 13) || in_range(step, 18, 19)) return "Промежуточный отжим";
      if (in_range(step, 14, 15)) return "Первое полоскание";
      if (in_range(step, 21, 22)) return "Второе полоскание";
      if (in_range(step, 24, 25)) return "Отжим";
      break;
  }
  return nullptr;
}

std::string active_options(const WasherStatus &status) {
  std::string options;
  const auto append = [&options](bool enabled, const char *name) {
    if (!enabled)
      return;
    if (!options.empty())
      options += ", ";
    options += name;
  };
  append(status.flags & 0x01, "Ускоренная стирка");
  append(status.flags & 0x02, "Предварительная стирка");
  append(status.flags & 0x08, "Звук выключен");
  append(status.flags & 0x10, "Доп. полоскание");
  append(status.flags & 0x20, "Защита от детей");
  append(status.flags & 0x80, "Пар");
  append(status.night_mode, "Ночной режим");
  return options.empty() ? "—" : options;
}

}  // namespace esphome::washer_uart
