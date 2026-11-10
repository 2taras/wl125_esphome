# ESPHome external components

This repository contains ESPHome external components maintained by `2taras`.

## Washer UART monitor

`washer_uart` listens to the washer's main-board TX line and reports the program,
cycle state, known stages, remaining time, delay, actual drum speed, door lock,
drying setting and active options in Home Assistant. All entities are optional.
The component marks numeric readings unknown at the next update if it has
received no valid status for 5 seconds. The component defaults to 2-second
updates, and the example uses a 10-second interval.

The measurement scales are for the DEXP WD-F10715DMA/WBSI used for calibration:

- `selected_temperature` is the setpoint in °C. The confirmed setpoints are 20,
  30, 40, 60 and 90 °C. Cold and unknown settings have no numeric value.
- `water_temperature` estimates °C during known water stages using an NTC
  divider model with beta 3368 K and a fitted resistance ratio of 0.3244.
  The fit uses three surface temperature measurements at codes 63–98. The
  divider and thermistor parameters are modeling assumptions. Codes 35–62 and
  99–174 extend the same curve and have greater uncertainty. Temperature during
  drying and stages whose meaning is unknown is omitted.
- `water_volume` estimates liters present relative to the start of a weighed
  fill. Codes 2561–2430 cover the fill from 0 to 7.73 L. Lower codes through
  2300 use a horizontal cylinder model. Codes above 2561 in the usual empty
  range show zero relative to the calibration baseline. This is a level estimate
  and does not include water absorbed in laundry or add up water use across fills.
- `water_temperature_basis` and `water_volume_basis` show whether the reading
  is within the measured range, extrapolated or unavailable. Raw codes are
  available as diagnostics.

See [the ESP32-S2 example](washer-uart.yaml). It uses `GPIO3`
as RX at 9600 baud, 8N1. Connect the main-board TX signal through a suitable
3.3 V level interface and use a shared signal ground on the isolated side of
the appliance. For capture in both directions, connect the display TX line
through the same type of interface to `GPIO5`. Both UARTs use RX pins only.
The example uses encrypted OTA, which requires ESPHome 2026.9.0 or later.
Set `wifi_ssid`, `wifi_password`, and `api_encryption_key` in
`secrets.yaml` alongside the example. OTA uses the same key as the Home
Assistant API.

### Optional UART capture

Capture is disabled by default. To enable it on an ESP32 with ESP-IDF and
ESPHome 2026.4.5 or later, add `web_server_base: {}` at the top level and
`capture: true` under the `washer_uart` sensor platform. To include display
traffic, define a second RX-only UART at 9600 baud, 8N1 and set
`display_uart_id` to its ID. The capture endpoint is available over HTTP
without authentication.

`GET /api/uart` returns up to 256 valid frames in arrival order. Each frame has
`id`, `ms` since boot, channel `ch` (0 for main-board TX, 1 for display TX) and
the frame body as `hex`. A collector can pass the returned `boot_id` and its
last frame ID as `?boot_id=…&after=…`. If the boot ID changes, the endpoint
returns the retained frames from the new boot. The buffer is in RAM and old
frames are overwritten when it fills.

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
button and voltage/current/instantaneous-power text sensors. Up to five real,
consecutive positive-going voltage cycles are captured from a 160-sample ring.
Each cycle is a separate `period_ms|v1,v2,...` state with 21 evenly spaced
points, keeping every state below Home Assistant's 255-character limit. A
`capture_id` marker is published last so HA can atomically combine the parts
into long template attributes. The endpoints are linearly interpolated zero
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
  voltage_2: {name: Waveform Voltage 2}
  voltage_3: {name: Waveform Voltage 3}
  voltage_4: {name: Waveform Voltage 4}
  voltage_5: {name: Waveform Voltage 5}
  current:
    name: Waveform Current
  current_2: {name: Waveform Current 2}
  current_3: {name: Waveform Current 3}
  current_4: {name: Waveform Current 4}
  current_5: {name: Waveform Current 5}
  power:
    name: Waveform Power
  power_2: {name: Waveform Power 2}
  power_3: {name: Waveform Power 3}
  power_4: {name: Waveform Power 4}
  power_5: {name: Waveform Power 5}
  capture_id: {name: Waveform Capture ID}
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
