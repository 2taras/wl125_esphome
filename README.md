# ESPHome external components

This repository contains ESPHome external components maintained by `2taras`.

## BL0906 SPI waveform meter

`bl0906_spi` is intended for six-channel BL0906 meters whose single voltage
input is used with currents from different mains phases. It reads the signed
`I1_WAVE..I6_WAVE` and `V_WAVE` registers over SPI and calculates power after
shifting the voltage waveform independently for every channel. The default
phase model is:

```text
channel 1: +120 degrees
channel 2:    0 degrees
channel 3: -120 degrees
channel 4:    0 degrees
channel 5:    0 degrees
channel 6:    0 degrees
```

This keeps nonlinear current waveforms intact. It is more accurate than
reconstructing power from RMS current and a single phase-angle register.

### IoTorero/WTR01/EM6 wiring used by this configuration

```text
BL0906 pin 24 SEL     -> ESP32-C3 GPIO3
BL0906 pin 14 SCLK    -> ESP32-C3 GPIO4
BL0906 pin 11 /CS     -> ESP32-C3 GPIO5
BL0906 pin  9 /RST    -> ESP32-C3 GPIO6
BL0906 pin 13 SDI/RX  <- ESP32-C3 GPIO7 (MOSI)
BL0906 pin 12 SDO/TX  -> ESP32-C3 GPIO8 (MISO)
```

The component drives `/RST` low, selects SPI with `SEL=1`, establishes
`CS=1/SCLK=0`, then releases reset. BL0906 SPI is Mode 1, MSB-first, and is
kept at 1 MHz (below the chip's documented 1.5 MHz maximum).

See [`examples/iotorero-em6-three-phase.yaml`](examples/iotorero-em6-three-phase.yaml)
for a complete configuration. To use it from GitHub:

```yaml
external_components:
  - source: github://2taras/wl125_esphome@main
    components: [bl0906_spi]
```

`phase_offsets` advances the sampled voltage waveform by the given angle. If
L2/L3 power has the wrong sign, swap `+120` and `-120`; that depends on the
actual phase order and CT direction. Corrected energy counts consumption only
(negative/export power is not added) and is saved to flash every five minutes.

The driver can publish apparent power, power factor, reactive power magnitude,
and phase angle for every channel from the same sampling window as current and
active power. It can also publish total apparent power and total power factor.

The optional `temperature` sensor reads the BL0906 internal temperature. The
optional `waveform` block exposes a six-channel selector, an on-demand capture
button and voltage/current/instantaneous-power text sensors. A capture contains
the most recent complete positive-going voltage cycle as
`period_ms|v1,v2,...`; 21 evenly spaced points keep every state below Home
Assistant's 255-character limit. The endpoints are linearly interpolated zero
crossings and power is the point-by-point physical product `U * I`.

```yaml
temperature:
  name: BL0906 Temperature
waveform:
  channel_names: [Grid L1, Grid L2, Grid L3, Channel 4, Channel 5, Channel 6]
  channel_select:
    name: Waveform Channel
  capture_button:
    name: Capture Waveform
  voltage:
    name: Waveform Voltage
  current:
    name: Waveform Current
  power:
    name: Waveform Power
```

The default voltage/current/power conversion coefficients are the same as the
stock ESPHome BL0906 component for Athom/IoTorero EM6 hardware. Use the
per-channel calibration options when comparing against a reference meter.

## WL125

The older WL125 component does not validate a CRC and must not be used for a
safety-critical access-control decision.

```yaml
external_components:
  - source:
      type: git
      url: https://github.com/2taras/wl125_esphome
uart:
  rx_pin: 5
  baud_rate: 9600
  id: wl125uart

wl125:
  uart_id: wl125uart
  on_tag:
    then:
      - homeassistant.tag_scanned: !lambda 'return to_string(x);'
```
