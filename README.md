# Toothbrush

Your electric toothbrush live on a Tessera screen. The zones you brush show as a ring around the time: the upper jaw
on top, the lower jaw below, a zone done in green and the one you brush now in the accent. Battery, pressure and mode
sit beside it, and a tap opens the whole session over the page.

It works with the toothbrushes Home Assistant knows over Bluetooth: Oral-B (`oralb`, `oralb_live`), Philips Sonicare
(`philips_sonicare_ble`), Xiaomi (`xiaomi_ble`) and Laifen (`laifen_ble`). It is a Tessera version of
[toothbrush-card](https://github.com/mtheli/toothbrush-card) by mtheli, whose zone and session logic it follows.

## Set up

1. In Tessera, open Plugins, choose **Add with a link** and paste this repository's link.
2. Tick the screen in the bathroom. Tessera asks for the brush's entities; choose them from the lists:

   | Asked for | Oral-B | Sonicare | Xiaomi | Laifen |
   |---|---|---|---|---|
   | Brushing state | its state sensor | handle state | the toothbrush entity | brushing state |
   | Zone | sector | sector | leave empty | leave empty |
   | Brushing time | time | brushing time | leave empty | leave empty |
   | Battery | battery | battery | battery | battery |
   | Pressure | pressure | pressure state | leave empty | leave empty |
   | Mode | mode | brushing mode | leave empty | mode |
   | Brush head | refill days | brush head wear | consumable | leave empty |
   | Last session | last session | last session | score | leave empty |

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

## Good to know

- The screen reads the brush's entities from Home Assistant itself. Nothing is needed in Home Assistant: no template,
  no helper, no integration of its own.
- The entities are chosen per screen when it is built, so a screen shows one brush. Choosing another brush builds the
  screen again.
- A brush without a zone sensor (Xiaomi, Laifen) gets its zones by time, evenly over the routine.
- Auto zones show 4 until the brush reports a fifth zone; choose 6 for a brush that brushes in six from the start.
- Tried on: not yet on a real screen. Please say in an issue which board and brush you used.

## Privacy

It talks to nothing outside your home: the screen reads the eight entities you chose from Home Assistant, over the
screen's own connection to it.
