"""binary_sensor platform for person_detect: person present / occupancy."""

import esphome.codegen as cg
from esphome.components import binary_sensor
import esphome.config_validation as cv

from . import CONF_PERSON_DETECT_ID, PersonDetector

DEPENDENCIES = ["person_detect"]

CONFIG_SCHEMA = binary_sensor.binary_sensor_schema(
    device_class="occupancy",
).extend(
    {
        cv.GenerateID(CONF_PERSON_DETECT_ID): cv.use_id(PersonDetector),
    }
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_PERSON_DETECT_ID])
    sens = await binary_sensor.new_binary_sensor(config)
    cg.add(parent.set_binary_sensor(sens))
