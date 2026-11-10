import esphome.codegen as cg
import esphome.config_validation as cv
import esphome.final_validate as fv
from esphome.components import sensor, text_sensor, uart
from esphome.const import (
    CONF_ID,
    CONF_TX_PIN,
    CONF_UART_ID,
    DEVICE_CLASS_DURATION,
    DEVICE_CLASS_TEMPERATURE,
    DEVICE_CLASS_VOLUME,
    ENTITY_CATEGORY_DIAGNOSTIC,
    STATE_CLASS_MEASUREMENT,
)
from esphome.core import CORE

DEPENDENCIES = ["uart"]
AUTO_LOAD = ["text_sensor"]

CONF_CAPTURE = "capture"
CONF_DISPLAY_UART_ID = "display_uart_id"

WasherUART = cg.esphome_ns.namespace("washer_uart").class_(
    "WasherUART", cg.PollingComponent, uart.UARTDevice
)

SENSORS = {
    "remaining_time": sensor.sensor_schema(
        device_class=DEVICE_CLASS_DURATION, unit_of_measurement="min", accuracy_decimals=0
    ),
    "delay_hours": sensor.sensor_schema(
        device_class=DEVICE_CLASS_DURATION, unit_of_measurement="h", accuracy_decimals=0
    ),
    "drum_speed": sensor.sensor_schema(
        unit_of_measurement="rpm",
        accuracy_decimals=0,
        state_class=STATE_CLASS_MEASUREMENT,
    ),
    "water_temperature": sensor.sensor_schema(
        device_class=DEVICE_CLASS_TEMPERATURE,
        unit_of_measurement="°C",
        accuracy_decimals=0,
        state_class=STATE_CLASS_MEASUREMENT,
    ),
    "selected_temperature": sensor.sensor_schema(
        device_class=DEVICE_CLASS_TEMPERATURE, unit_of_measurement="°C", accuracy_decimals=0
    ),
    "water_volume": sensor.sensor_schema(
        device_class=DEVICE_CLASS_VOLUME,
        unit_of_measurement="L",
        accuracy_decimals=1,
        state_class=STATE_CLASS_MEASUREMENT,
    ),
    **{
        key: sensor.sensor_schema(
            accuracy_decimals=0, entity_category=ENTITY_CATEGORY_DIAGNOSTIC
        )
        for key in ("temperature_code", "water_level_code", "program_code", "step_code", "state_code", "error_code")
    },
}

TEXT_SENSORS = {
    key: text_sensor.text_sensor_schema()
    for key in ("program", "cycle_state", "cycle_stage", "door_lock", "drying_setting", "active_options")
}
TEXT_SENSORS.update({
    key: text_sensor.text_sensor_schema(entity_category=ENTITY_CATEGORY_DIAGNOSTIC)
    for key in ("water_temperature_basis", "water_volume_basis")
})

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(WasherUART),
            cv.Optional(CONF_CAPTURE, default=False): cv.boolean,
            cv.Optional(CONF_DISPLAY_UART_ID): cv.use_id(uart.UARTComponent),
            **{cv.Optional(key): schema for key, schema in SENSORS.items()},
            **{cv.Optional(key): schema for key, schema in TEXT_SENSORS.items()},
        }
    )
    .extend(uart.UART_DEVICE_SCHEMA)
    .extend(cv.polling_component_schema("2s"))
)


def _rx_only(config):
    if CONF_TX_PIN in config:
        raise cv.Invalid("washer_uart requires a receive-only UART. Remove tx_pin.")
    return config


_validate_uart = {
    key: uart.final_validate_device_schema(
        "washer_uart", uart_bus=key, baud_rate=9600, require_rx=True, data_bits=8, parity="NONE", stop_bits=1
    )
    for key in (CONF_UART_ID, CONF_DISPLAY_UART_ID)
}

_validate_capture_version = cv.require_esphome_version(2026, 4, 5)


def _validate_bus(config, key):
    fv.id_declaration_match_schema(_rx_only)(config[key])
    _validate_uart[key](config)


def _validate_capture(config):
    _validate_capture_version(config)
    if not CORE.is_esp32 or CORE.using_arduino:
        raise cv.Invalid("washer_uart capture requires an ESP32 using ESP-IDF")
    full_config = fv.full_config.get()
    if "web_server_base" not in full_config.get_config_for_path([]):
        raise cv.Invalid("washer_uart capture requires web_server_base: {} or web_server:")
    if full_config.data.get("washer_uart_capture"):
        raise cv.Invalid("Only one washer_uart can expose /api/uart")
    full_config.data["washer_uart_capture"] = True
    if CONF_DISPLAY_UART_ID in config:
        if config[CONF_UART_ID] == config[CONF_DISPLAY_UART_ID]:
            raise cv.Invalid("display_uart_id must refer to a separate UART")
        _validate_bus(config, CONF_DISPLAY_UART_ID)


def FINAL_VALIDATE_SCHEMA(config):
    _validate_bus(config, CONF_UART_ID)
    if config[CONF_CAPTURE]:
        _validate_capture(config)
    elif CONF_DISPLAY_UART_ID in config:
        raise cv.Invalid("display_uart_id requires capture: true")
    return config


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)
    if config[CONF_CAPTURE]:
        cg.add_define("USE_WASHER_CAPTURE")
        cg.add(var.enable_capture())
        if CONF_DISPLAY_UART_ID in config:
            display_uart = await cg.get_variable(config[CONF_DISPLAY_UART_ID])
            cg.add(var.set_display_uart(display_uart))
    for key in SENSORS:
        if key in config:
            entity = await sensor.new_sensor(config[key])
            cg.add(getattr(var, f"set_{key}")(entity))
    for key in TEXT_SENSORS:
        if key in config:
            entity = await text_sensor.new_text_sensor(config[key])
            cg.add(getattr(var, f"set_{key}")(entity))
