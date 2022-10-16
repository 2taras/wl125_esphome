# wl125_esphome
This implementation isn't safe and doesn't have crc check
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
