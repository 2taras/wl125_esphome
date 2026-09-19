import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import pins
from esphome.components import button, select, sensor, spi, text_sensor
from esphome.const import (
    CONF_CURRENT,
    CONF_ENERGY,
    CONF_FREQUENCY,
    CONF_ID,
    CONF_NAME,
    CONF_POWER,
    CONF_TEMPERATURE,
    CONF_VOLTAGE,
    DEVICE_CLASS_CURRENT,
    DEVICE_CLASS_ENERGY,
    DEVICE_CLASS_FREQUENCY,
    DEVICE_CLASS_POWER,
    DEVICE_CLASS_TEMPERATURE,
    DEVICE_CLASS_VOLTAGE,
    ICON_CURRENT_AC,
    ICON_POWER,
    STATE_CLASS_MEASUREMENT,
    STATE_CLASS_TOTAL_INCREASING,
    UNIT_AMPERE,
    UNIT_HERTZ,
    UNIT_KILOWATT_HOURS,
    UNIT_CELSIUS,
    UNIT_VOLT,
    UNIT_WATT,
)

DEPENDENCIES = ["spi"]
AUTO_LOAD = ["button", "select", "sensor", "text_sensor"]

CONF_SELECT_PIN = "select_pin"
CONF_RESET_PIN = "reset_pin"
CONF_SAMPLE_RATE = "sample_rate"
CONF_PHASE_OFFSETS = "phase_offsets"
CONF_RESTORE_ENERGY = "restore_energy"
CONF_VOLTAGE_CALIBRATION = "voltage_calibration"
CONF_CURRENT_CALIBRATION = "current_calibration"
CONF_POWER_CALIBRATION = "power_calibration"
CONF_TOTAL_POWER = "total_power"
CONF_TOTAL_ENERGY = "total_energy"
CONF_TOTAL_APPARENT_POWER = "total_apparent_power"
CONF_TOTAL_POWER_FACTOR = "total_power_factor"
CONF_ACTUAL_SAMPLE_RATE = "actual_sample_rate"
CONF_SPI_ERRORS = "spi_errors"
CONF_RAW_VOLTAGE_RMS = "raw_voltage_rms"
CONF_RAW_CURRENT_RMS = "raw_current_rms"
CONF_RAW_POWER = "raw_power"
CONF_APPARENT_POWER = "apparent_power"
CONF_POWER_FACTOR = "power_factor"
CONF_REACTIVE_POWER = "reactive_power"
CONF_PHASE_ANGLE = "phase_angle"
CONF_WAVEFORM = "waveform"
CONF_CHANNEL_NAMES = "channel_names"
CONF_CHANNEL_SELECT = "channel_select"
CONF_CAPTURE_BUTTON = "capture_button"
CONF_CAPTURE_ID = "capture_id"
CONF_VOLTAGE_PARTS = [CONF_VOLTAGE, "voltage_2", "voltage_3", "voltage_4", "voltage_5"]
CONF_CURRENT_PARTS = [CONF_CURRENT, "current_2", "current_3", "current_4", "current_5"]
CONF_POWER_PARTS = [CONF_POWER, "power_2", "power_3", "power_4", "power_5"]

# Defaults match the coefficients used by ESPHome's stock BL0906 component for
# the Athom/IoTorero EM6 analogue front end.
DEFAULT_VOLTAGE_CALIBRATION = 8.334599604923265e-05
DEFAULT_CURRENT_CALIBRATION = 1.6706643822577576e-05
DEFAULT_POWER_CALIBRATION = 1.4234677637430922e-09

bl0906_spi_ns = cg.esphome_ns.namespace("bl0906_spi")
BL0906SPI = bl0906_spi_ns.class_(
    "BL0906SPI", cg.PollingComponent, spi.SPIDevice
)
BL0906WaveformChannelSelect = bl0906_spi_ns.class_(
    "BL0906WaveformChannelSelect", select.Select
)
BL0906WaveformCaptureButton = bl0906_spi_ns.class_(
    "BL0906WaveformCaptureButton", button.Button
)


def _raw_sensor_schema(icon):
    return sensor.sensor_schema(
        icon=icon,
        accuracy_decimals=0,
        state_class=STATE_CLASS_MEASUREMENT,
    )


CHANNEL_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_CURRENT): cv.maybe_simple_value(
            sensor.sensor_schema(
                icon=ICON_CURRENT_AC,
                accuracy_decimals=3,
                device_class=DEVICE_CLASS_CURRENT,
                unit_of_measurement=UNIT_AMPERE,
                state_class=STATE_CLASS_MEASUREMENT,
            ),
            key=CONF_NAME,
        ),
        cv.Optional(CONF_POWER): cv.maybe_simple_value(
            sensor.sensor_schema(
                icon=ICON_POWER,
                accuracy_decimals=1,
                device_class=DEVICE_CLASS_POWER,
                unit_of_measurement=UNIT_WATT,
                state_class=STATE_CLASS_MEASUREMENT,
            ),
            key=CONF_NAME,
        ),
        cv.Optional(CONF_ENERGY): cv.maybe_simple_value(
            sensor.sensor_schema(
                icon="mdi:lightning-bolt",
                accuracy_decimals=3,
                device_class=DEVICE_CLASS_ENERGY,
                unit_of_measurement=UNIT_KILOWATT_HOURS,
                state_class=STATE_CLASS_TOTAL_INCREASING,
            ),
            key=CONF_NAME,
        ),
        cv.Optional(CONF_RAW_CURRENT_RMS): _raw_sensor_schema(
            "mdi:chart-bell-curve"
        ),
        cv.Optional(CONF_RAW_POWER): _raw_sensor_schema("mdi:multiplication"),
        cv.Optional(CONF_APPARENT_POWER): cv.maybe_simple_value(
            sensor.sensor_schema(
                icon="mdi:flash",
                accuracy_decimals=1,
                device_class="apparent_power",
                unit_of_measurement="VA",
                state_class=STATE_CLASS_MEASUREMENT,
            ),
            key=CONF_NAME,
        ),
        cv.Optional(CONF_POWER_FACTOR): cv.maybe_simple_value(
            sensor.sensor_schema(
                icon="mdi:angle-acute",
                accuracy_decimals=1,
                device_class="power_factor",
                unit_of_measurement="%",
                state_class=STATE_CLASS_MEASUREMENT,
            ),
            key=CONF_NAME,
        ),
        cv.Optional(CONF_REACTIVE_POWER): cv.maybe_simple_value(
            sensor.sensor_schema(
                icon="mdi:sine-wave",
                accuracy_decimals=1,
                unit_of_measurement="var",
                state_class=STATE_CLASS_MEASUREMENT,
            ),
            key=CONF_NAME,
        ),
        cv.Optional(CONF_PHASE_ANGLE): cv.maybe_simple_value(
            sensor.sensor_schema(
                icon="mdi:angle-acute",
                accuracy_decimals=1,
                unit_of_measurement="°",
                state_class=STATE_CLASS_MEASUREMENT,
            ),
            key=CONF_NAME,
        ),
        cv.Optional(
            CONF_CURRENT_CALIBRATION, default=DEFAULT_CURRENT_CALIBRATION
        ): cv.positive_float,
        cv.Optional(
            CONF_POWER_CALIBRATION, default=DEFAULT_POWER_CALIBRATION
        ): cv.float_,
    }
)


def _phase_offsets(value):
    values = cv.ensure_list(cv.float_)(value)
    if len(values) != 6:
        raise cv.Invalid("phase_offsets must contain exactly 6 angles")
    for angle in values:
        if angle < -180.0 or angle > 180.0:
            raise cv.Invalid("phase offsets must be between -180 and 180 degrees")
    return values


def _channel_names(value):
    values = cv.ensure_list(cv.string_strict)(value)
    if len(values) != 6:
        raise cv.Invalid("waveform.channel_names must contain exactly 6 names")
    if any(not name.strip() for name in values):
        raise cv.Invalid("waveform channel names cannot be empty")
    if len(set(values)) != len(values):
        raise cv.Invalid("waveform channel names must be unique")
    return values


WAVEFORM_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_CHANNEL_NAMES): _channel_names,
        cv.Required(CONF_CHANNEL_SELECT): select.select_schema(
            BL0906WaveformChannelSelect,
            icon="mdi:electric-switch",
        ),
        cv.Required(CONF_CAPTURE_BUTTON): button.button_schema(
            BL0906WaveformCaptureButton,
            icon="mdi:camera-waveform",
        ),
        cv.Required(CONF_VOLTAGE): text_sensor.text_sensor_schema(
            icon="mdi:sine-wave"
        ),
        cv.Required(CONF_CURRENT): text_sensor.text_sensor_schema(
            icon="mdi:current-ac"
        ),
        cv.Required(CONF_POWER): text_sensor.text_sensor_schema(
            icon="mdi:flash"
        ),
        cv.Optional("voltage_2"): text_sensor.text_sensor_schema(icon="mdi:sine-wave"),
        cv.Optional("voltage_3"): text_sensor.text_sensor_schema(icon="mdi:sine-wave"),
        cv.Optional("voltage_4"): text_sensor.text_sensor_schema(icon="mdi:sine-wave"),
        cv.Optional("voltage_5"): text_sensor.text_sensor_schema(icon="mdi:sine-wave"),
        cv.Optional("current_2"): text_sensor.text_sensor_schema(icon="mdi:current-ac"),
        cv.Optional("current_3"): text_sensor.text_sensor_schema(icon="mdi:current-ac"),
        cv.Optional("current_4"): text_sensor.text_sensor_schema(icon="mdi:current-ac"),
        cv.Optional("current_5"): text_sensor.text_sensor_schema(icon="mdi:current-ac"),
        cv.Optional("power_2"): text_sensor.text_sensor_schema(icon="mdi:flash"),
        cv.Optional("power_3"): text_sensor.text_sensor_schema(icon="mdi:flash"),
        cv.Optional("power_4"): text_sensor.text_sensor_schema(icon="mdi:flash"),
        cv.Optional("power_5"): text_sensor.text_sensor_schema(icon="mdi:flash"),
        cv.Optional(CONF_CAPTURE_ID): text_sensor.text_sensor_schema(
            icon="mdi:identifier"
        ),
    }
)


CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(BL0906SPI),
            cv.Required(CONF_SELECT_PIN): pins.gpio_output_pin_schema,
            cv.Required(CONF_RESET_PIN): pins.gpio_output_pin_schema,
            cv.Optional(CONF_SAMPLE_RATE, default="1000Hz"): cv.All(
                cv.frequency, cv.Range(min=100, max=2000)
            ),
            cv.Optional(
                CONF_PHASE_OFFSETS, default=[120.0, 0.0, -120.0, 0.0, 0.0, 0.0]
            ): _phase_offsets,
            cv.Optional(CONF_RESTORE_ENERGY, default=True): cv.boolean,
            cv.Optional(
                CONF_VOLTAGE_CALIBRATION, default=DEFAULT_VOLTAGE_CALIBRATION
            ): cv.positive_float,
            cv.Optional(CONF_VOLTAGE): sensor.sensor_schema(
                icon="mdi:sine-wave",
                accuracy_decimals=1,
                device_class=DEVICE_CLASS_VOLTAGE,
                unit_of_measurement=UNIT_VOLT,
                state_class=STATE_CLASS_MEASUREMENT,
            ),
            cv.Optional(CONF_FREQUENCY): sensor.sensor_schema(
                icon="mdi:sine-wave",
                accuracy_decimals=2,
                device_class=DEVICE_CLASS_FREQUENCY,
                unit_of_measurement=UNIT_HERTZ,
                state_class=STATE_CLASS_MEASUREMENT,
            ),
            cv.Optional(CONF_TEMPERATURE): sensor.sensor_schema(
                icon="mdi:thermometer",
                accuracy_decimals=1,
                device_class=DEVICE_CLASS_TEMPERATURE,
                unit_of_measurement=UNIT_CELSIUS,
                state_class=STATE_CLASS_MEASUREMENT,
            ),
            cv.Optional(CONF_WAVEFORM): WAVEFORM_SCHEMA,
            cv.Optional(CONF_TOTAL_POWER): sensor.sensor_schema(
                icon=ICON_POWER,
                accuracy_decimals=1,
                device_class=DEVICE_CLASS_POWER,
                unit_of_measurement=UNIT_WATT,
                state_class=STATE_CLASS_MEASUREMENT,
            ),
            cv.Optional(CONF_TOTAL_ENERGY): sensor.sensor_schema(
                icon="mdi:lightning-bolt",
                accuracy_decimals=3,
                device_class=DEVICE_CLASS_ENERGY,
                unit_of_measurement=UNIT_KILOWATT_HOURS,
                state_class=STATE_CLASS_TOTAL_INCREASING,
            ),
            cv.Optional(CONF_TOTAL_APPARENT_POWER): sensor.sensor_schema(
                icon="mdi:flash",
                accuracy_decimals=1,
                device_class="apparent_power",
                unit_of_measurement="VA",
                state_class=STATE_CLASS_MEASUREMENT,
            ),
            cv.Optional(CONF_TOTAL_POWER_FACTOR): sensor.sensor_schema(
                icon="mdi:angle-acute",
                accuracy_decimals=1,
                device_class="power_factor",
                unit_of_measurement="%",
                state_class=STATE_CLASS_MEASUREMENT,
            ),
            cv.Optional(CONF_ACTUAL_SAMPLE_RATE): sensor.sensor_schema(
                icon="mdi:speedometer",
                accuracy_decimals=0,
                unit_of_measurement=UNIT_HERTZ,
                state_class=STATE_CLASS_MEASUREMENT,
            ),
            cv.Optional(CONF_SPI_ERRORS): sensor.sensor_schema(
                icon="mdi:alert-circle-outline",
                accuracy_decimals=0,
                state_class=STATE_CLASS_MEASUREMENT,
            ),
            cv.Optional(CONF_RAW_VOLTAGE_RMS): _raw_sensor_schema(
                "mdi:chart-bell-curve"
            ),
        }
    )
    .extend(
        cv.Schema(
            {cv.Optional(f"channel_{i + 1}"): CHANNEL_SCHEMA for i in range(6)}
        )
    )
    .extend(
        spi.spi_device_schema(
            cs_pin_required=True, default_data_rate="1MHz", default_mode="MODE1"
        )
    )
    .extend(cv.polling_component_schema("1s"))
)

FINAL_VALIDATE_SCHEMA = spi.final_validate_device_schema(
    "bl0906_spi", require_mosi=True, require_miso=True
)


async def _new_sensor(config, key, setter, var):
    if sensor_config := config.get(key):
        sens = await sensor.new_sensor(sensor_config)
        cg.add(getattr(var, setter)(sens))


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await spi.register_spi_device(var, config)

    select_pin = await cg.gpio_pin_expression(config[CONF_SELECT_PIN])
    reset_pin = await cg.gpio_pin_expression(config[CONF_RESET_PIN])
    cg.add(var.set_select_pin(select_pin))
    cg.add(var.set_reset_pin(reset_pin))
    cg.add(var.set_sample_rate(config[CONF_SAMPLE_RATE]))
    cg.add(var.set_restore_energy(config[CONF_RESTORE_ENERGY]))
    cg.add(var.set_voltage_calibration(config[CONF_VOLTAGE_CALIBRATION]))

    for index, offset in enumerate(config[CONF_PHASE_OFFSETS]):
        cg.add(var.set_phase_offset(index, offset))

    await _new_sensor(config, CONF_VOLTAGE, "set_voltage_sensor", var)
    await _new_sensor(config, CONF_FREQUENCY, "set_frequency_sensor", var)
    await _new_sensor(config, CONF_TEMPERATURE, "set_temperature_sensor", var)
    await _new_sensor(config, CONF_TOTAL_POWER, "set_total_power_sensor", var)
    await _new_sensor(config, CONF_TOTAL_ENERGY, "set_total_energy_sensor", var)
    await _new_sensor(
        config, CONF_TOTAL_APPARENT_POWER, "set_total_apparent_power_sensor", var
    )
    await _new_sensor(
        config, CONF_TOTAL_POWER_FACTOR, "set_total_power_factor_sensor", var
    )
    await _new_sensor(
        config, CONF_ACTUAL_SAMPLE_RATE, "set_actual_sample_rate_sensor", var
    )
    await _new_sensor(config, CONF_SPI_ERRORS, "set_spi_errors_sensor", var)
    await _new_sensor(
        config, CONF_RAW_VOLTAGE_RMS, "set_raw_voltage_rms_sensor", var
    )

    if waveform_config := config.get(CONF_WAVEFORM):
        channel_select = await select.new_select(
            waveform_config[CONF_CHANNEL_SELECT],
            options=waveform_config[CONF_CHANNEL_NAMES],
        )
        cg.add(channel_select.set_parent(var))
        cg.add(var.set_waveform_channel_select(channel_select))

        capture_button = await button.new_button(
            waveform_config[CONF_CAPTURE_BUTTON]
        )
        cg.add(capture_button.set_parent(var))
        cg.add(var.set_waveform_capture_button(capture_button))

        for keys, setter in (
            (CONF_VOLTAGE_PARTS, "set_waveform_voltage_sensor"),
            (CONF_CURRENT_PARTS, "set_waveform_current_sensor"),
            (CONF_POWER_PARTS, "set_waveform_power_sensor"),
        ):
            for period, key in enumerate(keys):
                if sensor_config := waveform_config.get(key):
                    waveform_sensor = await text_sensor.new_text_sensor(
                        sensor_config
                    )
                    cg.add(getattr(var, setter)(period, waveform_sensor))

        if capture_id_config := waveform_config.get(CONF_CAPTURE_ID):
            capture_id_sensor = await text_sensor.new_text_sensor(
                capture_id_config
            )
            cg.add(var.set_waveform_capture_id_sensor(capture_id_sensor))

    for index in range(6):
        channel = config.get(f"channel_{index + 1}")
        if channel is None:
            continue
        cg.add(
            var.set_current_calibration(
                index, channel[CONF_CURRENT_CALIBRATION]
            )
        )
        cg.add(
            var.set_power_calibration(index, channel[CONF_POWER_CALIBRATION])
        )
        for key, setter in (
            (CONF_CURRENT, "set_current_sensor"),
            (CONF_POWER, "set_power_sensor"),
            (CONF_ENERGY, "set_energy_sensor"),
            (CONF_RAW_CURRENT_RMS, "set_raw_current_rms_sensor"),
            (CONF_RAW_POWER, "set_raw_power_sensor"),
            (CONF_APPARENT_POWER, "set_apparent_power_sensor"),
            (CONF_POWER_FACTOR, "set_power_factor_sensor"),
            (CONF_REACTIVE_POWER, "set_reactive_power_sensor"),
            (CONF_PHASE_ANGLE, "set_phase_angle_sensor"),
        ):
            if sensor_config := channel.get(key):
                sens = await sensor.new_sensor(sensor_config)
                cg.add(getattr(var, setter)(index, sens))
