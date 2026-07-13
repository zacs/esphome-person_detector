"""sensor platform for person_detect: confidence, person count, ambient light.

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
      - platform: person_detect
        person_detect_id: presence
        type: illuminance
        name: "Ambient Light"

`illuminance` is a relative brightness (0-100% of full scale) derived from the
mean frame luma, not a calibrated lux reading. With the sensor at fixed
exposure/gain it tracks room light monotonically; it clips toward 100% in very
bright scenes and reads unknown while the privacy switch idles the camera.
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
TYPE_ILLUMINANCE = "illuminance"

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
    # Relative brightness, so % (not lx / device_class illuminance, which would
    # imply a calibrated absolute reading we can't give).
    TYPE_ILLUMINANCE: dict(
        unit_of_measurement=UNIT_PERCENT,
        accuracy_decimals=0,
        state_class=STATE_CLASS_MEASUREMENT,
        icon="mdi:brightness-percent",
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
                {t: t for t in _TYPE_DEFAULTS}, lower=True
            ),
        }
    )(config)


CONFIG_SCHEMA = _schema

_SETTERS = {
    TYPE_CONFIDENCE: "set_confidence_sensor",
    TYPE_COUNT: "set_count_sensor",
    TYPE_ILLUMINANCE: "set_illuminance_sensor",
}


async def to_code(config):
    parent = await cg.get_variable(config[CONF_PERSON_DETECT_ID])
    sens = await sensor.new_sensor(config)
    cg.add(getattr(parent, _SETTERS[config[CONF_TYPE]])(sens))
