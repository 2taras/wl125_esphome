import math
import unittest


OUTPUT_POINTS = 21


def positive_crossings(samples):
    result = []
    for index in range(1, len(samples)):
        left = samples[index - 1]
        right = samples[index]
        if left[1] < 0.0 <= right[1]:
            ratio = -left[1] / (right[1] - left[1])
            result.append(
                (
                    left[0] + ratio * (right[0] - left[0]),
                    left[2] + ratio * (right[2] - left[2]),
                    index,
                )
            )
    return result


def capture_periods(
    samples,
    voltage_calibration,
    current_calibration,
    max_periods=5,
    max_gap_ms=3.0,
):
    crossings = positive_crossings(samples)
    if len(crossings) < 2:
        return []
    candidates = []
    for start, end in zip(crossings[:-1], crossings[1:]):
        start_time, start_current, start_right = start
        end_time, end_current, end_right = end
        if 12.0 <= end_time - start_time <= 30.0 and all(
            samples[index][0] - samples[index - 1][0] <= max_gap_ms
            for index in range(start_right + 1, end_right + 1)
        ):
            candidates.append((start, end))
        else:
            candidates.append(None)

    best = []
    current = []
    for candidate in candidates:
        if candidate is None:
            current = []
            continue
        current.append(candidate)
        current = current[-max_periods:]
        if len(current) >= len(best):
            best = list(current)

    return [
        resample_period(
            samples, selected, voltage_calibration, current_calibration
        )
        for selected in best
    ]


def resample_period(samples, selected, voltage_calibration, current_calibration):
    (start_time, start_current, start_right), (
        end_time,
        end_current,
        end_right,
    ) = selected
    period = end_time - start_time
    output = []
    segment = start_right
    for output_index in range(OUTPUT_POINTS):
        target = start_time + period * output_index / (OUTPUT_POINTS - 1)
        if output_index == 0:
            raw_voltage, raw_current = 0.0, start_current
        elif output_index == OUTPUT_POINTS - 1:
            raw_voltage, raw_current = 0.0, end_current
        else:
            while segment < end_right and samples[segment][0] <= target:
                segment += 1
            left, right = samples[segment - 1], samples[segment]
            ratio = (target - left[0]) / (right[0] - left[0])
            raw_voltage = left[1] + ratio * (right[1] - left[1])
            raw_current = left[2] + ratio * (right[2] - left[2])
        voltage = raw_voltage * voltage_calibration
        current = raw_current * current_calibration
        output.append((voltage, current, voltage * current))
    return period, output


def capture_period(samples, voltage_calibration, current_calibration, max_gap_ms=3.0):
    captures = capture_periods(
        samples,
        voltage_calibration,
        current_calibration,
        max_periods=1,
        max_gap_ms=max_gap_ms,
    )
    return captures[-1] if captures else None


def make_samples(frequency, phase_offset, count=160, sample_rate=1000, sequence_start=0):
    result = []
    for sequence in range(sequence_start, sequence_start + count):
        time_s = sequence / sample_rate
        angle = 2 * math.pi * frequency * time_s
        shifted_voltage = 325.0 * math.sin(angle + math.radians(phase_offset))
        current = 10.0 * math.sin(angle + math.radians(phase_offset))
        result.append((time_s * 1000.0, shifted_voltage, current))
    return result


def encode(period_ms, values, decimals):
    return f"{period_ms:.3f}|" + ",".join(
        f"{value:.{decimals}f}" for value in values
    )


class WaveformCaptureTest(unittest.TestCase):
    def test_extracts_supported_frequencies_and_phase_offsets(self):
        for frequency in (45, 50, 60):
            for phase_offset in (-120, 0, 120):
                with self.subTest(frequency=frequency, phase_offset=phase_offset):
                    capture = capture_period(
                        make_samples(frequency, phase_offset), 1.0, 1.0
                    )
                    self.assertIsNotNone(capture)
                    period, points = capture
                    self.assertAlmostEqual(period, 1000.0 / frequency, delta=0.08)
                    self.assertEqual(len(points), OUTPUT_POINTS)
                    self.assertAlmostEqual(points[0][0], 0.0)
                    self.assertAlmostEqual(points[-1][0], 0.0)

    def test_scale_and_instantaneous_power_sign(self):
        period, points = capture_period(make_samples(50, 0), 2.0, 0.5)
        peak = points[5]
        trough = points[15]
        self.assertAlmostEqual(peak[0], 650.0, delta=7.0)
        self.assertAlmostEqual(peak[1], 5.0, delta=0.1)
        self.assertGreater(peak[2], 0.0)
        self.assertGreater(trough[2], 0.0)
        self.assertAlmostEqual(peak[2], peak[0] * peak[1], places=9)

    def test_latest_period_survives_ring_wrap(self):
        # A 128-entry ring whose logical sequence has wrapped many times is
        # presented chronologically to the extraction routine.
        samples = make_samples(50, -120, sequence_start=9973)
        period, points = capture_period(samples, 1.0, 1.0)
        self.assertAlmostEqual(period, 20.0, delta=0.08)
        self.assertEqual(len(points), OUTPUT_POINTS)

    def test_falls_back_when_latest_period_contains_a_gap(self):
        samples = make_samples(50, 0)
        # Damage the latest cycle while leaving several earlier cycles intact.
        damaged = samples[:-8] + [
            (time_ms + 5.0, voltage, current)
            for time_ms, voltage, current in samples[-8:]
        ]
        period, points = capture_period(damaged, 1.0, 1.0)
        self.assertAlmostEqual(period, 20.0, delta=0.08)
        self.assertEqual(len(points), OUTPUT_POINTS)

    def test_payloads_fit_home_assistant_state_limit(self):
        period, points = capture_period(make_samples(45, 120), 1.0, 1.0)
        voltage = encode(period, [point[0] for point in points], 1)
        current = encode(period, [point[1] for point in points], 3)
        power = encode(period, [point[2] for point in points], 1)
        self.assertLessEqual(len(voltage), 255)
        self.assertLessEqual(len(current), 255)
        self.assertLessEqual(len(power), 255)

    def test_five_real_periods_fit_ring_at_43_hz_and_above(self):
        for frequency in (43, 45, 50, 60):
            for phase_offset in (-120, 0, 120):
                with self.subTest(frequency=frequency, phase_offset=phase_offset):
                    captures = capture_periods(
                        make_samples(frequency, phase_offset), 1.0, 1.0
                    )
                    self.assertEqual(len(captures), 5)
                    for period, points in captures:
                        self.assertAlmostEqual(
                            period, 1000.0 / frequency, delta=0.08
                        )
                        self.assertEqual(len(points), OUTPUT_POINTS)

    def test_capture_never_uses_more_than_available_buffer_periods(self):
        captures = capture_periods(
            make_samples(43, 0, count=105), 1.0, 1.0
        )
        self.assertLess(len(captures), 5)
        self.assertTrue(captures)

    def test_five_parts_combine_into_long_home_assistant_attribute(self):
        captures = capture_periods(make_samples(43, 120), 1.0, 1.0)
        voltage_parts = [
            encode(period, [point[0] for point in points], 1)
            for period, points in captures
        ]
        self.assertTrue(all(len(part) <= 255 for part in voltage_parts))
        self.assertGreater(len(";".join(voltage_parts)), 255)

    def test_all_six_channel_choices(self):
        offsets = (0, 0, 0, 120, 0, -120)
        for channel, offset in enumerate(offsets):
            with self.subTest(channel=channel):
                period, points = capture_period(make_samples(50, offset), 1.0, 1.0)
                self.assertAlmostEqual(period, 20.0, delta=0.08)
                self.assertEqual(len(points), OUTPUT_POINTS)


if __name__ == "__main__":
    unittest.main()
