# Toothbrush

Your electric toothbrush live on a Tessera screen. The zones you brush show as a ring around the time: the upper jaw
on top, the lower jaw below, a zone done in green and the one you brush now in the accent. Battery, pressure and mode
sit beside it, and a tap opens the whole session over the page.

It works with the toothbrushes Home Assistant knows over Bluetooth: Oral-B (`oralb`, `oralb_live`), Philips Sonicare
(`philips_sonicare_ble`), Xiaomi (`xiaomi_ble`) and Laifen (`laifen_ble`). It is a Tessera version of
[toothbrush-card](https://github.com/mtheli/toothbrush-card) by mtheli, whose zone and session logic it follows.

## Set up

1. In Tessera, open Plugins, choose **Add with a link** and paste this repository's link.
2. Tick the screen in the bathroom. Tessera asks for the **Toothbrush**: choose the brush's main entity, and leave the
   other fields empty. The plugin finds the rest of the brush from it:

   | Brush | Choose as Toothbrush | Found beside it |
   |---|---|---|
   | Oral-B (`oralb`, `oralb_live`) | `sensor.<brush>_toothbrush_state` | sector, time, battery, pressure, mode, refill days, last session |
   | Sonicare (`philips_sonicare_ble`) | `sensor.<brush>_handle_state` | sector, brushing time, battery, pressure state, brushing mode, last session |
   | Laifen (`laifen_ble`) | `sensor.<brush>_status` | timer, battery, pressing too hard, mode |
   | Xiaomi (`xiaomi_ble`) | `sensor.<brush>_toothbrush` | battery, consumable, score |

   Fill in one of the other fields only when the plugin cannot find that entity: when Home Assistant runs in another
   language (its entity ids are then in that language), when you renamed an entity, and for a Sonicare's brush head,
   which is a device of its own (choose its brush head wear). The screen's log lists every entity it follows, with
   "(found)" beside the ones it found itself.

   Tessera lists every sensor of the house in these fields: it cannot yet offer only the entities of one device.
3. Apply. The screen builds once.
4. In Layout, place the **Toothbrush** tile from the library's Plugins group, at any size from 1x1 to 4x4.
5. In the inspector, check the options: the number of zones (Auto, 4 or 6), the length of your routine, how long the
   result stays, and what the brush head counts (days, percent or sessions).

## What it shows

- **1x1**: the ring with the time in its middle.
- **2x1 and wider**: the ring on the left; the zone, how far you are and the chips on the right.
- **1x2 and taller**: the ring on top; the zone, how far you are and the chips under it.
- **2x2 and up**: the name, battery, pressure and mode, and a large ring.
- **A tap**: the full-screen card, with every zone named around the ring and every reading.

After a session the ring stays green for as long as you chose ("Done"), or shows how far you got ("Stopped early").

![The plugin on the 10-inch, the 4-inch square and the CYD](store.png)

The full-screen card on a 10-inch screen, in the middle of a six-zone session:

![The card on the Guition 10.1-inch, dark](renders/jc8012p4a1/card-zones-dark.png)

Every tile at every size on every screen, drawn by the real firmware on Tessera's preview bench, is in
[renders/](renders/) ([overview](renders/overview.png)): one folder per screen, light and dark, with the finest grid of
each in a folder of its own.

## Good to know

- The screen reads the brush's entities from Home Assistant itself. Nothing is needed in Home Assistant: no template,
  no helper, no integration of its own.
- The entities are chosen per screen when it is built, so a screen shows one brush. Choosing another brush builds the
  screen again.
- A brush without a zone sensor (Xiaomi, Laifen) gets its zones by time, evenly over the routine.
- Auto zones show 4 until the brush reports a fifth zone; choose 6 for a brush that brushes in six from the start.
- Tried on: Tessera's preview bench (the CYD, the Guition 4-inch, the Waveshare 4.3-inch standing up and the Guition
  10.1-inch), not yet on a real screen with a real brush. Please say in an issue which board and brush you used.

## Privacy

It talks to nothing outside your home: the screen reads the toothbrush's entities from Home Assistant (the one you
chose, the ones it finds beside it and any you name), over the screen's own connection to it.
