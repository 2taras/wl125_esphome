"""Host tests run the actual driver processing/restore code; only SPI/interpolation is replaced.
Run: python3 -m unittest discover -s tests -p 'test_*.py' -v
"""
import pathlib
import re
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
COMP = ROOT / 'components' / 'bl0906_spi'


def function(source, signature):
    start = source.index(signature)
    brace = source.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


class EnergyTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        header = (COMP / 'bl0906_spi.h').read_text()
        source = (COMP / 'bl0906_spi.cpp').read_text()
        members = header[header.index('  std::array<double, BL0906_CHANNEL_COUNT> previous_power_w_'):header.index('  uint32_t acquired_since_publish_')]
        sample = function(header, 'struct BL0906WaveSample') + ';'
        body = function(source, 'void BL0906SPI::process_sample_')
        state = function(header, 'struct BL0906EnergyRestoreState') + ';'
        save = function(source, 'void BL0906SPI::save_energy_')
        restore_match = re.search(r'(?:std::copy\(restored\.energy_kwh[^;]+;|this->energy_kwh_ = restored.energy_kwh;)', source)
        assert restore_match is not None, 'Driver energy-restore path must be covered'
        restore = restore_match.group()
        helper = '#include "energy_window.h"\nusing esphome::bl0906_spi::BL0906EnergyWindow;' if (COMP / 'energy_window.h').exists() else ''
        harness = r'''
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
HELPER
constexpr size_t BL0906_CHANNEL_COUNT=6;
SAMPLE
STATE
struct Preference {BL0906EnergyRestoreState saved{};void save(const BL0906EnergyRestoreState *s){saved=*s;}};
struct BL0906SPI {
MEMBERS
bool preference_ready_{true};Preference energy_preference_;
void save_energy_();
void restore(const BL0906EnergyRestoreState &restored) {RESTORE}
std::array<double,6> phase_offsets_deg_{};
std::array<float,6> power_calibration_{{1,1,1,1,1,1}};
std::array<double,6> current_square_sum_{}, product_sum_{};
double voltage_square_sum_{0}; float frequency_hz_{50}; uint32_t processed_sample_count_{0};
bool interpolate_voltage_(uint32_t, double &out) const {out=1;return true;}
void process_sample_(const BL0906WaveSample &sample);
void feed(double watts,uint32_t timestamp) {BL0906WaveSample s;s.voltage=1;s.current[0]=std::lround(watts);s.current_time_us.fill(timestamp);process_sample_(s);}
};
BODY
SAVE
int main(int argc,char** argv) {
const int mode=std::atoi(argv[1]);BL0906SPI meter;
const double initial=argc>2 ? std::atof(argv[2]) : 0;
meter.energy_kwh_[0]=initial;
if(mode==5){
 static_assert(sizeof(BL0906EnergyRestoreState)==24,"Legacy flash layout must not change");
 BL0906EnergyRestoreState legacy;legacy.energy_kwh={{30.25f,44.125f,8.5f,0.25f,2.5f,7.75f}};
 meter.restore(legacy);
 for(size_t i=0;i<6;i++)if(meter.energy_kwh_[i]!=legacy.energy_kwh[i])return 2;
 meter.feed(1000,0);for(uint32_t t=1000;t<=10000000;t+=1000)meter.feed(1000,t);
 meter.save_energy_();BL0906SPI rebooted;rebooted.restore(meter.energy_preference_.saved);
 for(size_t i=1;i<6;i++)if(rebooted.energy_kwh_[i]!=legacy.energy_kwh[i])return 3;
 std::printf("%.15g\n",double(rebooted.energy_kwh_[0])-legacy.energy_kwh[0]);return 0;
}
if(mode==8){
 meter.feed(1000,0);
 for(uint32_t t=1000;t<=10000;t+=1000)meter.feed(1000,t);
 meter.feed(1000,200000); // Do not bridge this 190 ms acquisition gap.
 for(uint32_t t=201000;t<=240000;t+=1000)meter.feed(1000,t);
 std::printf("%.15g\n",double(meter.energy_kwh_[0])-initial);return 0;
}
if(mode==9){
 const uint32_t steps[]={799,1301,777,1223};uint32_t t=0;size_t i=0;
 meter.feed(1000,t);
 while(t<1000000){t+=std::min(steps[i++%4],1000000-t);meter.feed(1000,t);}
 std::printf("%.15g\n",double(meter.energy_kwh_[0])-initial);return 0;
}
uint32_t timestamp=mode==4 ? UINT32_MAX-10000 : 0;
const uint32_t dt=mode==3 ? 1250 : 1000;
const uint32_t duration=mode==0 ? 3600000000u : 10000000u;
const double pi=std::acos(-1.0);
for(uint32_t i=0;i<=duration/dt;i++) {
 const double t=double(i)*dt/1e6;
 double watts=1000;
 if(mode==1) watts=1000*std::sin(2*2*pi*50*t);
 if(mode==2) watts=-1000;
 if(mode==3) watts=1000*(1-std::cos(2*2*pi*50*t));
 if(mode==6) watts=1000+2000*std::sin(2*2*pi*50*t);
 if(mode==7) watts=-1000+2000*std::sin(2*2*pi*50*t);
 meter.feed(watts,timestamp);timestamp+=dt;
}
std::printf("%.15g\n",double(meter.energy_kwh_[0])-initial);
}
'''
        for key, value in [('HELPER', helper), ('SAMPLE', sample), ('STATE', state), ('MEMBERS', members), ('RESTORE',restore), ('BODY', body), ('SAVE',save)]:
            harness = harness.replace(key, value)
        cls.tmp = tempfile.TemporaryDirectory()
        p = pathlib.Path(cls.tmp.name)
        (p / 'energy_test.cpp').write_text(harness)
        cls.binary = p / 'energy_test'
        subprocess.run(['g++','-std=c++17','-O2','-Wall','-Wextra','-Werror','-fno-fast-math','-I',str(COMP),str(p/'energy_test.cpp'),'-o',str(cls.binary)],check=True)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def run_case(self, mode, initial=0):
        return float(subprocess.check_output([str(self.binary),str(mode),str(initial)],text=True))

    def test_accumulates_one_kwh_at_existing_counter_values(self):
        for initial in [0,8,16,30,44,1000]:
            with self.subTest(initial=initial):
                self.assertAlmostEqual(self.run_case(0,initial),1.0,delta=1e-6)

    def test_pure_reactive_power_does_not_count_as_active_import(self):
        self.assertAlmostEqual(self.run_case(1,30),0,delta=1e-8)

    def test_export_does_not_increase_consumption(self):
        self.assertEqual(self.run_case(2,30),0)

    def test_resistive_waveform_at_800_hz(self):
        self.assertAlmostEqual(self.run_case(3,30),1000*10/3600000,delta=1e-6)

    def test_micros_wrap_preserves_energy(self):
        self.assertAlmostEqual(self.run_case(4,30),1000*10/3600000,delta=1e-6)

    def test_legacy_flash_layout_and_reboot_preserve_all_six_counters(self):
        self.assertAlmostEqual(self.run_case(5),1000*10/3600000,delta=2e-6)

    def test_reactive_exchange_does_not_inflate_positive_active_power(self):
        self.assertAlmostEqual(self.run_case(6,30),1000*10/3600000,delta=1e-6)

    def test_negative_average_with_positive_pulses_is_not_import(self):
        self.assertEqual(self.run_case(7,30),0)

    def test_sampling_gap_drops_partial_window_without_fabricating_energy(self):
        self.assertAlmostEqual(self.run_case(8,30),1000*0.04/3600000,delta=1e-10)

    def test_irregular_sampling_splits_intervals_at_period_boundaries(self):
        self.assertAlmostEqual(self.run_case(9,30),1000/3600000,delta=1e-10)


if __name__ == '__main__':
    unittest.main()
