"""Toothbrush: the brushing zones of an electric toothbrush as a ring, live, from the brush's own entities.

The eight entities are ESPHome homeassistant text sensors of plugin.yaml; this component reads them, follows the
session (toothbrush.cpp) and registers the tile and the card. register_plugin() takes the id, version, tile memory and
the screen's texts from the manifest and translations/ beside this folder.
"""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import smart_display, text_sensor
from esphome.const import CONF_ID

DEPENDENCIES = ["smart_display"]

SENSORS = ("state", "sector", "time", "battery", "pressure", "mode", "head", "last")

toothbrush_ns = cg.esphome_ns.namespace("toothbrush")
Toothbrush = toothbrush_ns.class_("Toothbrush", cg.Component)

CONFIG_SCHEMA = cv.Schema(
    {cv.GenerateID(): cv.declare_id(Toothbrush)}
    | {cv.Required(name): cv.use_id(text_sensor.TextSensor) for name in SENSORS}
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await smart_display.register_plugin(var, __file__)
    for name in SENSORS:
        cg.add(getattr(var, f"set_{name}")(await cg.get_variable(config[name])))
