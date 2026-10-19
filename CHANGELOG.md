# Changelog

## 0.4.0
- The brush's entities are the tile's options now (plugin API 0.15): choose the toothbrush, and its other entities
  for the readings you want, in the tile's inspector. Tessera sends them and follows them; another choice needs no
  build and no restart.
- Each tile has its own brush, so one screen can show several.
- Needs a Tessera with plugin API 0.15 (its dev branch until the next release).
- Update: the toothbrush input and the entity settings are gone; choose the same entities in each tile's options.

## 0.3.0
- One field when you add the plugin: the toothbrush. Tessera asks for every field it shows, so the other seven are
  gone from there, and the plugin can be applied with the toothbrush alone.
- Another entity for a reading is now a setting: the plugin's details on the screen's Plugins tab, On this screen.
  Leave it empty to find it beside the toothbrush; the screen restarts once when you change one.
- Update: if you filled in the other fields before, fill the same entity ids into the settings.

## 0.2.1
- A wide tile on a small screen shows only the lines that fit: the zone first, then the name, a line of detail and the
  chips.
- A small ring shows the timer's icon where a word would not fit.
- The names around the card's ring are the side alone (Left, Front, Right), and go away where even those would crowd
  the ring.
- Tried on the preview bench: the CYD, the Guition 4-inch, the Waveshare 4.3-inch standing up and the Guition 10.1-inch,
  light and dark, on their own grids and the finest ones.

## 0.2.0
- Choose your toothbrush once: pick its main entity, and the plugin finds the brush's other entities itself (Oral-B,
  Sonicare, Laifen and Xiaomi, by their English entity names).
- The other inputs are optional now: fill one in only when your entity has another name (a Home Assistant in another
  language, a renamed entity, or a Sonicare's brush head).
- Update: open the plugin's details on the screen's Plugins tab, choose the toothbrush entity, and build once.

## 0.1.0
- First version: your electric toothbrush live on a tile, with the zones as a ring of 4 or 6 arcs, the time, battery
  and pressure, and a full-screen card with every reading.
- Reads the brush's own entities directly: choose them when you add the plugin to a screen. Nothing to set up in Home
  Assistant.
