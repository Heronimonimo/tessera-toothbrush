#pragma once
#include "esphome/components/smart_display/plugin_api.h"

namespace esphome::toothbrush {

class Toothbrush;

// The brushing zones as a ring: 4 or 6 arcs with round ends, the upper jaw on the top half and the lower jaw on the
// bottom half, and a gap of 4 % of the circle between every two, measured between the round ends, so no two arcs ever
// touch. A zone to do is the track's grey, the one brushed now the accent, a zone done Home Assistant's green.
class Ring {
 public:
  static constexpr float GAP_DEGREES = 0.04f * 360.0f;   // 14.4°

  // Make the parts in `parent` (in create() or open(), as every object): six arcs, and with `labels` (the card) six
  // names, enough for either count of zones. The ring's outer edge is a circle of `diameter` around (cx, cy); with
  // labels it shrinks until every name fits in the square of `diameter` too.
  void create(lv_obj_t *parent, int cx, int cy, int diameter, const Toothbrush *plugin, bool labels);
  // Lay the parts out for 4 or 6 zones. Only sizes, angles and texts change: the count may change while the tile
  // lives (a brush shows it names six zones once it reports a fifth).
  void set_zones(int zones);
  // Colour the arcs: `done` zones in brushing order are done, `active` is brushed now (-1 for none).
  void paint(int active, int done);
  void on_theme();
  // The room inside the ring, across, for the timer in its middle.
  int inner() const { return inner_; }
  int zones() const { return zones_; }

 private:
  lv_obj_t *arcs_[6]{};
  lv_obj_t *labels_[6]{};
  int8_t look_[6] = {-1, -1, -1, -1, -1, -1};   // what each arc shows: 0 to do, 1 now, 2 done
  const Toothbrush *plugin_ = nullptr;
  int cx_ = 0, cy_ = 0, diameter_ = 0, zones_ = 0, inner_ = 0;
  bool labels_on_ = false;
  void colour(int slot, int8_t look);
};

}  // namespace esphome::toothbrush
