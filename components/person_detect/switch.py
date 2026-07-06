"""switch platform for person_detect: runtime privacy toggle.

When turned off, detection stops and the camera stream is released, so the
sensor does no work at all (privacy). Defaults to on (restored on boot).
"""

import esphome.codegen as cg
from esphome.components import switch
import esphome.config_validation as cv

from . import CONF_PERSON_DETECT_ID, PersonDetector, person_detect_ns

DEPENDENCIES = ["person_detect"]

PersonDetectSwitch = person_detect_ns.class_(
    "PersonDetectSwitch", switch.Switch, cg.Parented.template(PersonDetector)
)

CONFIG_SCHEMA = switch.switch_schema(
    PersonDetectSwitch,
    default_restore_mode="RESTORE_DEFAULT_ON",
    icon="mdi:motion-sensor",
).extend(
    {
        cv.GenerateID(CONF_PERSON_DETECT_ID): cv.use_id(PersonDetector),
    }
)


async def to_code(config):
    sw = await switch.new_switch(config)
    await cg.register_parented(sw, config[CONF_PERSON_DETECT_ID])
