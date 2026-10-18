#include "ring.h"
#include "toothbrush.h"
#include <algorithm>
#include <cmath>

namespace esphome::toothbrush {

using tessera::Font;
using tessera::Tone;
namespace ui = tessera::ui;

static constexpr float PI = 3.14159265f;

// Where each zone of the brushing order sits on the ring. The ring's slots run clockwise from 9 o'clock, so the upper
// jaw is the top half (left to right) and the lower jaw the bottom half (right to left):
//   4 zones: slot 0 upper left, 1 upper right, 2 lower right, 3 lower left;
//   6 zones: slot 0 upper left, 1 upper front, 2 upper right, 3 lower right, 4 lower front, 5 lower left.
// The brushing order is the toothbrush card's (QUADRANT_ZONES, SEXTANT_ZONES): lower left first.
static int slot_of(int zones, int index) {
  static const int FOUR[] = {3, 2, 0, 1};        // lower left, lower right, upper left, upper right
  static const int SIX[] = {5, 4, 3, 2, 1, 0};   // lower left, lower front, lower right, upper right, upper front, upper left
  return zones == 6 ? SIX[index] : FOUR[index];
}

// The zone of the brushing order that sits in a slot.
static int index_of(int zones, int slot) {
  for (int i = 0; i < zones; ++i)
    if (slot_of(zones, i) == slot) return i;
  return 0;
}

static void show(lv_obj_t *obj, bool shown) {
  if (!obj || lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN) != shown) return;
  if (shown) lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
}

void Ring::create(lv_obj_t *parent, int cx, int cy, int diameter, const Toothbrush *plugin, bool labels) {
  plugin_ = plugin;
  cx_ = cx;
  cy_ = cy;
  diameter_ = diameter;
  labels_on_ = labels;
  for (int s = 0; s < 6; ++s) {
    lv_obj_t *arc = lv_arc_create(parent);
    lv_obj_remove_style_all(arc);
    lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_arc_set_rotation(arc, 0);
    lv_obj_set_style_arc_rounded(arc, true, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(arc, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(arc, LV_OPA_TRANSP, LV_PART_INDICATOR);
    arcs_[s] = arc;
    if (labels) {
      labels_[s] = ui::label(parent, Font::BODY, theme::MUTED);
      lv_obj_set_style_text_align(labels_[s], LV_TEXT_ALIGN_CENTER, 0);
    }
  }
  set_zones(4);
}

void Ring::set_zones(int zones) {
  zones = zones == 6 ? 6 : 4;
  if (zones == zones_) return;
  zones_ = zones;
  const float slot = 360.0f / zones_;
  int width = std::max(ui::px(4), diameter_ * 7 / 100);
  float radius = diameter_ / 2.0f - width / 2.0f;   // the middle of the arc

  // With names outside the arcs (the side of the jaw: the half of the ring says which jaw), the ring gives way until
  // each name, a gap away from its arc, stays in the square. Where the names would touch each other, or leave the
  // ring less than 60 % of its square, it goes without: the card's column names the zone brushed now anyway.
  const int gap = ui::px(6), name_h = ui::line_height(Font::BODY);
  int name_w[6] = {}, name_x[6] = {}, name_y[6] = {};
  bool named = labels_on_;
  if (named) {
    float r = radius;
    for (int s = 0; s < zones_; ++s) {
      name_w[s] = ui::text_width(plugin_->side_name(zones_, index_of(zones_, s)), Font::BODY) + ui::px(2);
      const float mid = (180.0f + (s + 0.5f) * slot) * PI / 180.0f;
      const float c = std::fabs(std::cos(mid)), sn = std::fabs(std::sin(mid));
      // The name's middle lies `reach` beyond the arc's middle: half the arc, the gap, and half the name that way.
      const float reach = width / 2.0f + gap + c * name_w[s] / 2.0f + sn * name_h / 2.0f;
      if (c > 0.01f) r = std::min(r, (diameter_ / 2.0f - name_w[s] / 2.0f) / c - reach);
      if (sn > 0.01f) r = std::min(r, (diameter_ / 2.0f - name_h / 2.0f) / sn - reach);
    }
    for (int s = 0; s < zones_; ++s) {
      const float mid = (180.0f + (s + 0.5f) * slot) * PI / 180.0f;
      const float c = std::cos(mid), sn = std::sin(mid);
      const float reach = r + width / 2.0f + gap + std::fabs(c) * name_w[s] / 2.0f + std::fabs(sn) * name_h / 2.0f;
      name_x[s] = cx_ + static_cast<int>(c * reach) - name_w[s] / 2;
      name_y[s] = cy_ + static_cast<int>(sn * reach) - name_h / 2;
    }
    for (int a = 0; a < zones_ && named; ++a)
      for (int b = a + 1; b < zones_; ++b)
        if (name_x[a] < name_x[b] + name_w[b] + gap && name_x[b] < name_x[a] + name_w[a] + gap &&
            name_y[a] < name_y[b] + name_h && name_y[b] < name_y[a] + name_h)
          named = false;
    if (2 * r + width < diameter_ * 0.6f) named = false;
    if (named) radius = r;
  }
  radius = std::max(radius, static_cast<float>(width));

  // Round ends reach half the arc's width past its angles: take that off both ends, so the gap one sees is the gap.
  // Whole degrees only, rounded inwards, so a gap can only grow. A ring too thick for its size gets thinner.
  int start[6] = {}, end[6] = {};
  for (;;) {
    const float cap = (width / 2.0f) / radius * 180.0f / PI;
    bool fits = true;
    for (int s = 0; s < zones_; ++s) {
      start[s] = static_cast<int>(std::ceil(180.0f + s * slot + GAP_DEGREES / 2.0f + cap));
      end[s] = static_cast<int>(std::floor(180.0f + (s + 1) * slot - GAP_DEGREES / 2.0f - cap));
      if (end[s] - start[s] < 6) fits = false;
    }
    if (fits || width <= 2) break;
    width -= 1;
  }

  const int box = static_cast<int>(2 * radius) + width;
  for (int s = 0; s < 6; ++s) {
    show(arcs_[s], s < zones_);
    show(labels_[s], named && s < zones_);
    look_[s] = -1;
    if (s >= zones_) continue;
    lv_obj_set_size(arcs_[s], box, box);
    lv_obj_set_pos(arcs_[s], cx_ - box / 2, cy_ - box / 2);
    lv_obj_set_style_arc_width(arcs_[s], width, LV_PART_MAIN);
    lv_arc_set_bg_angles(arcs_[s], start[s] % 360, end[s] % 360);
    if (labels_[s] && named) {
      lv_obj_set_size(labels_[s], name_w[s], name_h);
      lv_obj_set_pos(labels_[s], name_x[s], name_y[s]);
      ui::set_text(labels_[s], plugin_->side_name(zones_, index_of(zones_, s)));
    }
    colour(s, 0);
  }
  inner_ = std::max(0, box - 2 * width);
}

void Ring::colour(int slot, int8_t look) {
  if (!arcs_[slot] || look_[slot] == look) return;
  look_[slot] = look;
  const lv_color_t c = look == 2   ? ui::tone_color(Tone::GOOD, theme::TRACK)
                       : look == 1 ? ui::tone_color(Tone::ACCENT, theme::ACCENT)
                                   : ui::color(theme::TRACK);
  lv_obj_set_style_arc_color(arcs_[slot], c, LV_PART_MAIN);
  if (labels_[slot]) ui::set_color(labels_[slot], look == 1 ? theme::INK : theme::MUTED);
}

void Ring::paint(int active, int done) {
  for (int i = 0; i < zones_; ++i) colour(slot_of(zones_, i), i == active ? 1 : i < done ? 2 : 0);
}

void Ring::on_theme() {
  for (int s = 0; s < zones_; ++s) {
    const int8_t look = look_[s];
    look_[s] = -1;   // the roles have other values now: colour every arc again
    colour(s, look < 0 ? 0 : look);
  }
}

}  // namespace esphome::toothbrush
