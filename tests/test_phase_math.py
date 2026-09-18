import math
import unittest


def mean_shifted_power(current_phase_deg, configured_offset_deg, samples=1000):
    total = 0.0
    for index in range(samples):
        angle = 2.0 * math.pi * index / samples
        current = math.sin(angle + math.radians(current_phase_deg))
        shifted_voltage = math.sin(angle + math.radians(configured_offset_deg))
        total += current * shifted_voltage
    return total / samples


class PhaseOffsetTest(unittest.TestCase):
    def test_requested_three_phase_offsets_recover_resistive_power(self):
        phases = (0.0, 120.0, -120.0, 0.0, 0.0, 0.0)
        for phase in phases:
            with self.subTest(phase=phase):
                self.assertAlmostEqual(
                    mean_shifted_power(phase, phase), 0.5, places=12
                )

    def test_unshifted_reference_has_wrong_sign_on_l2_and_l3(self):
        self.assertAlmostEqual(mean_shifted_power(120.0, 0.0), -0.25, places=12)
        self.assertAlmostEqual(mean_shifted_power(-120.0, 0.0), -0.25, places=12)

    def test_swapping_phase_order_is_detectable(self):
        self.assertAlmostEqual(mean_shifted_power(120.0, -120.0), -0.25, places=12)
        self.assertAlmostEqual(mean_shifted_power(-120.0, 120.0), -0.25, places=12)


if __name__ == "__main__":
    unittest.main()
