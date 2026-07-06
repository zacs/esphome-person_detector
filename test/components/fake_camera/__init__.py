"""Test-only stub camera.

ESPHome's modular `camera` framework currently ships a C++-only base
(`esphome::camera::Camera`) with no configurable Python platform in the
release, so there is nothing for `person_detect`'s `camera_id` to reference in
CI. This stub provides a minimal component whose id is an `esphome::camera::Camera`
subclass, letting us validate (`esphome config`) and build (`esphome compile`)
person_detect end-to-end without real camera hardware. It delivers no frames.

NOT shipped — lives under test/ and is only loaded by the CI test config.
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID

# Pull in the camera component's C++ sources (the base class) so FakeCamera can
# subclass esphome::camera::Camera and link.
AUTO_LOAD = ["camera"]

camera_ns = cg.esphome_ns.namespace("camera")
Camera = camera_ns.class_("Camera")

fake_camera_ns = cg.esphome_ns.namespace("fake_camera")
FakeCamera = fake_camera_ns.class_("FakeCamera", cg.Component, Camera)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(FakeCamera),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
