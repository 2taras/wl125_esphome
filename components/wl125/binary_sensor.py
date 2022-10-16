import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor, wl125
from esphome.const import CONF_UID
from . import wl125_ns

DEPENDENCIES = ["wl125"]

CONF_WL125_ID = "wl125_id"
WL125BinarySensor = wl125_ns.class_(
    "WL125BinarySensor", binary_sensor.BinarySensor
)

CONFIG_SCHEMA = binary_sensor.binary_sensor_schema(WL125BinarySensor).extend(
    {
        cv.GenerateID(CONF_WL125_ID): cv.use_id(wl125.WL125Component),
        cv.Required(CONF_UID): cv.uint32_t,
    }
)


async def to_code(config):
    var = await binary_sensor.new_binary_sensor(config)

    hub = await cg.get_variable(config[CONF_WL125_ID])
    cg.add(hub.register_card(var))
    cg.add(var.set_id(config[CONF_UID]))
