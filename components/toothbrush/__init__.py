"""Toothbrush: the brushing zones of an electric toothbrush as a ring, live, from the brush's own entities.

The toothbrush input names the brush's main entity; the component finds the brush's other entities beside it by their
names (brush.cpp), unless an input names one, and follows each one the way a homeassistant text sensor does. It
registers the tile and the card (toothbrush.cpp). register_plugin() takes the id, version, tile memory and the screen's
texts from the manifest and translations/ beside this folder.
"""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import smart_display
from esphome.const import CONF_ID

DEPENDENCIES = ["smart_display", "api"]

# The readings in the order of the component's Role, and of the manifest's inputs.
ROLES = ("state", "sector", "time", "battery", "pressure", "mode", "head", "last")

toothbrush_ns = cg.esphome_ns.namespace("toothbrush")
Toothbrush = toothbrush_ns.class_("Toothbrush", cg.Component)


def entity_or_empty(value):
    """An entity id, or "" for an input left empty: that reading is then found beside the toothbrush entity."""
    value = cv.string(value).strip()
    return cv.entity_id(value) if value else ""


CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(Toothbrush),
        cv.Required("state"): cv.entity_id,
        **{cv.Optional(role, default=""): entity_or_empty for role in ROLES[1:]},
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await smart_display.register_plugin(var, __file__)
    for index, role in enumerate(ROLES):
        cg.add(var.set_entity(index, config[role]))
    # The component follows Home Assistant's states itself, as a homeassistant sensor does.
    cg.add_define("USE_API_HOMEASSISTANT_STATES")
