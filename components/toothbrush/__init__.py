"""Toothbrush: the brushing zones of an electric toothbrush as a ring, live, from the brush's own entities.

The toothbrush input names the brush's main entity; the component finds the brush's other entities beside it by their
names (brush.cpp), unless a setting of plugin.yaml (a text entity) names one, and follows each one the way a
homeassistant text sensor does. It registers the tile and the card (toothbrush.cpp). register_plugin() takes the id,
version, tile memory and the screen's texts from the manifest and translations/ beside this folder.
"""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import smart_display, text
from esphome.const import CONF_ID

DEPENDENCIES = ["smart_display", "api", "text"]

# The readings in the order of the component's Role: the toothbrush first, then the ones a setting may name.
ROLES = ("state", "sector", "time", "battery", "pressure", "mode", "head", "last")

toothbrush_ns = cg.esphome_ns.namespace("toothbrush")
Toothbrush = toothbrush_ns.class_("Toothbrush", cg.Component)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(Toothbrush),
        cv.Required("state"): cv.entity_id,
        **{cv.Required(role): cv.use_id(text.Text) for role in ROLES[1:]},
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await smart_display.register_plugin(var, __file__)
    cg.add(var.set_entity(0, config["state"]))
    for index, role in enumerate(ROLES[1:], start=1):
        cg.add(var.set_override(index, await cg.get_variable(config[role])))
    # The component follows Home Assistant's states itself, as a homeassistant sensor does.
    cg.add_define("USE_API_HOMEASSISTANT_STATES")
