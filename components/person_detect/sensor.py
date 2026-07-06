"""sensor platform for person_detect: detection confidence and/or person count.

Add one entry per metric:

    sensor:
      - platform: person_detect
        person_detect_id: presence
        type: confidence
        name: "Presence Confidence"
      - platform: person_detect
        person_detect_id: presence
        type: count
        name: "People Count"
"""

import esphome.codegen as cg
from esphome.components import sensor
import esphome.config_validation as cv
from esphome.const import (
    CONF_TYPE,
    STATE_CLASS_MEASUREMENT,
    UNIT_EMPTY,
    UNIT_PERCENT,
)

from . import CONF_PERSON_DETECT_ID, PersonDetector

DEPENDENCIES = ["person_detect"]

TYPE_CONFIDENCE = "confidence"
TYPE_COUNT = "count"

# Per-type unit / accuracy defaults; the user can still override in YAML.
_TYPE_DEFAULTS = {
    TYPE_CONFIDENCE: dict(
        unit_of_measurement=UNIT_PERCENT,
        accuracy_decimals=0,
        state_class=STATE_CLASS_MEASUREMENT,
    ),
    TYPE_COUNT: dict(
        unit_of_measurement=UNIT_EMPTY,
        accuracy_decimals=0,
        state_class=STATE_CLASS_MEASUREMENT,
    ),
}


def _schema(config):
    # Pick unit/accuracy defaults from the requested type before building the
    # sensor schema, so `confidence` reports % and `count` reports a bare number.
    type_ = config.get(CONF_TYPE, TYPE_CONFIDENCE)
    if type_ not in _TYPE_DEFAULTS:
        raise cv.Invalid(
            f"'{type_}' is not a valid type; expected one of "
            f"{', '.join(_TYPE_DEFAULTS)}",
            path=[CONF_TYPE],
        )
    return sensor.sensor_schema(**_TYPE_DEFAULTS[type_]).extend(
        {
            cv.GenerateID(CONF_PERSON_DETECT_ID): cv.use_id(PersonDetector),
            cv.Optional(CONF_TYPE, default=TYPE_CONFIDENCE): cv.enum(
                {TYPE_CONFIDENCE: TYPE_CONFIDENCE, TYPE_COUNT: TYPE_COUNT}, lower=True
            ),
        }
    )(config)


CONFIG_SCHEMA = _schema


async def to_code(config):
    parent = await cg.get_variable(config[CONF_PERSON_DETECT_ID])
    sens = await sensor.new_sensor(config)
    if config[CONF_TYPE] == TYPE_CONFIDENCE:
        cg.add(parent.set_confidence_sensor(sens))
    else:
        cg.add(parent.set_count_sensor(sens))
