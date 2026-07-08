"""esp_video_camera — ESP32-P4 MIPI-CSI raw frame source for person_detect.

Drives a CSI sensor (SC2356 on the reTerminal D1001) through Espressif's
esp_video (V4L2) + ISP stack and the PPA, and exposes an
esphome::person_detect::FrameSource that yields upright RGB888 frames. Wire it to
person_detect via `frame_source_id`.

This owns the camera hardware, unlike the ESPHome-camera JPEG path — it exists
because stock ESPHome has no configurable MIPI-CSI camera platform yet.
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import pins
from esphome.components.esp32 import add_idf_component, add_idf_sdkconfig_option
from esphome.components.person_detect import FrameSource
from esphome.const import (
    CONF_FREQUENCY,
    CONF_ID,
    CONF_RESET_PIN,
    CONF_RESOLUTION,
    CONF_ROTATION,
    CONF_SCL,
    CONF_SDA,
)

CODEOWNERS = ["@zacs"]
DEPENDENCIES = ["person_detect", "esp32"]

esp_video_camera_ns = cg.esphome_ns.namespace("esp_video_camera")
# Inherits person_detect::FrameSource so person_detect's use_id(FrameSource)
# accepts it.
EspVideoCamera = esp_video_camera_ns.class_(
    "EspVideoCamera", cg.Component, FrameSource
)

# esp_video (pulls esp_cam_sensor / esp_cam_ctlr / esp_driver_isp transitively).
# Pin for reproducibility; bump if the esp_video_init API changes. 2.x tracks
# current ESP-IDF (5.5.x) and ships the SC202CS driver; the old 0.9.0 pin bundled
# an esp_cam_sensor that still used the SC2356 name and predates IDF 5.5.4.
ESP_VIDEO_COMPONENT = "espressif/esp_video"
ESP_VIDEO_REF = "2.2.0"

CONF_POWERDOWN_PIN = "powerdown_pin"
CONF_ENABLE_PIN = "enable_pin"
CONF_I2C_PORT = "i2c_port"
CONF_SENSOR = "sensor"
CONF_SWAP_RGB = "swap_rgb"
CONF_FRAME_BUFFER_COUNT = "frame_buffer_count"

# The reTerminal D1001's camera is marketed as "SC2356"; the esp_cam_sensor
# driver for that part is named SC202CS (SC2356 is the same die).
SENSOR_SC2356 = "sc2356"

# resolution -> (width, height, esp_cam_sensor default-format Kconfig choice).
# The SC202CS RAW8 1280x720@30 mode is the sensor driver's default; we select it
# explicitly so the capture geometry is deterministic.
RESOLUTIONS = {
    "1280x720": (
        1280,
        720,
        "CONFIG_CAMERA_SC202CS_MIPI_DEFAULT_FMT_RAW8_1280X720_30FPS",
    ),
}

ROTATIONS = [0, 90, 180, 270]

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(EspVideoCamera),
        # Sensor SCCB (I2C) — reTerminal D1001: SDA=GPIO37, SCL=GPIO38.
        cv.Optional(CONF_SDA, default=37): cv.int_range(min=0, max=56),
        cv.Optional(CONF_SCL, default=38): cv.int_range(min=0, max=56),
        # Dedicated I2C controller for the SCCB. MUST differ from the port the
        # ESPHome `i2c:` bus uses (that one is port 0 when it's declared first,
        # e.g. the D1001 expander bus), else the master-bus install collides and
        # the sensor is never detected. P4 has controllers 0 and 1.
        cv.Optional(CONF_I2C_PORT, default=1): cv.int_range(min=0, max=1),
        cv.Optional(CONF_FREQUENCY, default="100kHz"): cv.All(
            cv.frequency, cv.int_range(min=1)
        ),
        # Power/reset control lines. On the D1001 these are on the PCA9535/XL9535
        # I2C expander — declare a pca9554 (PCA9535 variant) hub and pass its
        # pins here. Use `inverted:` to match each line's active level.
        cv.Optional(CONF_ENABLE_PIN): pins.gpio_output_pin_schema,
        cv.Optional(CONF_POWERDOWN_PIN): pins.gpio_output_pin_schema,
        cv.Optional(CONF_RESET_PIN): pins.gpio_output_pin_schema,
        cv.Optional(CONF_SENSOR, default=SENSOR_SC2356): cv.enum(
            {SENSOR_SC2356: SENSOR_SC2356}, lower=True
        ),
        cv.Optional(CONF_RESOLUTION, default="1280x720"): cv.enum(
            {k: k for k in RESOLUTIONS}, lower=True
        ),
        cv.Optional(CONF_ROTATION, default=0): cv.one_of(*ROTATIONS, int=True),
        cv.Optional(CONF_SWAP_RGB, default=False): cv.boolean,
        cv.Optional(CONF_FRAME_BUFFER_COUNT, default=2): cv.int_range(min=2, max=4),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    cg.add(var.set_sccb_pins(config[CONF_SDA], config[CONF_SCL]))
    cg.add(var.set_sccb_port(config[CONF_I2C_PORT]))
    cg.add(var.set_sccb_freq(int(config[CONF_FREQUENCY])))

    if CONF_ENABLE_PIN in config:
        cg.add(var.set_enable_pin(await cg.gpio_pin_expression(config[CONF_ENABLE_PIN])))
    if CONF_POWERDOWN_PIN in config:
        cg.add(
            var.set_powerdown_pin(
                await cg.gpio_pin_expression(config[CONF_POWERDOWN_PIN])
            )
        )
    if CONF_RESET_PIN in config:
        cg.add(var.set_reset_pin(await cg.gpio_pin_expression(config[CONF_RESET_PIN])))

    width, height, mode_kconfig = RESOLUTIONS[config[CONF_RESOLUTION]]
    cg.add(var.set_capture_size(width, height))
    cg.add(var.set_rotation(config[CONF_ROTATION]))
    cg.add(var.set_swap_rgb(config[CONF_SWAP_RGB]))
    cg.add(var.set_frame_buffer_count(config[CONF_FRAME_BUFFER_COUNT]))

    # esp_video managed component + build-time selection of the CSI/ISP path and
    # the sensor. Kconfig symbol names verified against espressif/esp-video-components
    # (esp_video/Kconfig and esp_cam_sensor/sensors/sc202cs/Kconfig.sc202cs).
    add_idf_component(name=ESP_VIDEO_COMPONENT, ref=ESP_VIDEO_REF)
    # MIPI-CSI + ISP video devices default to y on the P4, but the ISP *pipeline
    # controller* — needed to demosaic the sensor's RAW8 Bayer output into RGB —
    # does not, so enable it (and the ISP device it depends on) explicitly.
    add_idf_sdkconfig_option("CONFIG_ESP_VIDEO_ENABLE_MIPI_CSI_VIDEO_DEVICE", True)
    add_idf_sdkconfig_option("CONFIG_ESP_VIDEO_ENABLE_ISP_VIDEO_DEVICE", True)
    add_idf_sdkconfig_option("CONFIG_ESP_VIDEO_ENABLE_ISP_PIPELINE_CONTROLLER", True)
    if config[CONF_SENSOR] == SENSOR_SC2356:
        # Enable the SC202CS driver AND its MIPI auto-detect: esp_video_init only
        # probes sensors whose auto-detect fn is registered, so without this no
        # sensor is found and no /dev/video0 is created (the v0.1.5 failure).
        add_idf_sdkconfig_option("CONFIG_CAMERA_SC202CS", True)
        add_idf_sdkconfig_option(
            "CONFIG_CAMERA_SC202CS_AUTO_DETECT_MIPI_INTERFACE_SENSOR", True
        )
        add_idf_sdkconfig_option(mode_kconfig, True)

    cg.add_define("USE_ESP_VIDEO_CAMERA")
