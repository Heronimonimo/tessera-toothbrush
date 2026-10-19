"""Toothbrush: the brushing zones of an electric toothbrush as a ring, live, from the brush's own entities.

The tile's options name the brush's entities (kind entity, plugin API 0.15); the app sends their states with the tile's
data, and the component follows the session from them (brush.cpp) and registers the tile and the card
(toothbrush.cpp). register_plugin() takes the id, version, tile memory and the screen's texts from the manifest and
translations/ beside this folder.
"""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import smart_display
from esphome.const import CONF_ID

DEPENDENCIES = ["smart_display"]

toothbrush_ns = cg.esphome_ns.namespace("toothbrush")
Toothbrush = toothbrush_ns.class_("Toothbrush", cg.Component)

CONFIG_SCHEMA = cv.Schema({cv.GenerateID(): cv.declare_id(Toothbrush)}).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await smart_display.register_plugin(var, __file__)
