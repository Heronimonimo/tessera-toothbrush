#include "toothbrush.h"
#include "ring.h"
#include "esphome/core/hal.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace esphome::toothbrush {

using tessera::Font;
using tessera::Tone;
namespace ui = tessera::ui;

// Icons of Tessera's set (screen_manager/app/tile_icons.py).
static constexpr uint32_t ICON_BATTERY_LEVELS[] = {0xF007A, 0xF007B, 0xF007C, 0xF007D, 0xF007E,
                                                   0xF007F, 0xF0080, 0xF0081, 0xF0082, 0xF0079};  // 10 % to full
static constexpr uint32_t ICON_BATTERY_ALERT = 0xF0083;
static constexpr uint32_t ICON_BATTERY_CHARGING = 0xF0084;
static constexpr uint32_t ICON_GAUGE = 0xF029A;

// ---- Drawing helpers shared by the tile and the card ----

static Options options_of(JsonObjectConst o) {
  Options x;
  const char *zones = o["zones"] | "auto";
  strncpy(x.zones, zones, sizeof x.zones - 1);
  x.routine = o["routine"] | 120;
  x.hold = o["hold"] | 2;
  const char *head = o["head"] | "days";
  strncpy(x.head, head, sizeof x.head - 1);
  x.show_name = o["show_name"] | true;
  return x;
}

static void show(lv_obj_t *obj, bool shown) {
  if (!obj || lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN) != shown) return;
  if (shown) lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
}

static void place(lv_obj_t *obj, int x, int y) {
  if (lv_obj_get_x(obj) != x || lv_obj_get_y(obj) != y) lv_obj_set_pos(obj, x, y);
}

static void resize(lv_obj_t *obj, int w, int h) {
  if (lv_obj_get_width(obj) != w || lv_obj_get_height(obj) != h) lv_obj_set_size(obj, std::max(1, w), std::max(1, h));
}

// The largest of the screen's fonts `text` fits in, `width` across and `height` high; the large digits only for a
// text they have every character of.
static Font fit(const std::string &text, int width, int height) {
  for (Font f : {Font::DIGITS, Font::VALUE, Font::HEADLINE, Font::TITLE, Font::BODY_LARGE}) {
    if (f == Font::DIGITS && !ui::covers(text, f)) continue;
    if (ui::text_width(text, f) <= width && ui::line_height(f) <= height) return f;
  }
  return Font::BODY;
}

// A reading in a row of chips: an icon of Tessera's set and a few words, in the colour of what it means.
struct Chip {
  lv_obj_t *icon{}, *words{};
  void create(lv_obj_t *parent) {
    icon = ui::label(parent, Font::ICON_SMALL, theme::INK_SOFT);
    words = ui::label(parent, Font::BODY, theme::INK_SOFT);
  }
  // Show it at x of the row at y, as wide as it needs; returns its width, 0 when it shows nothing or does not fit.
  int put(int x, int y, int room, uint32_t glyph, const std::string &text, Tone tone) {
    const std::string mark = glyph ? ui::icon(glyph) : "";
    const int iw = glyph ? ui::text_width(mark, Font::ICON_SMALL) : 0;
    const int tw = ui::text_width(text, Font::BODY) + ui::px(2);
    const int w = iw + (iw ? ui::px(4) : 0) + tw;
    const bool fits = !text.empty() && w <= room;
    show(icon, fits && glyph);
    show(words, fits);
    if (!fits) return 0;
    const int line = std::max(ui::line_height(Font::BODY), ui::line_height(Font::ICON_SMALL));
    if (glyph) {
      ui::set_text(icon, mark);
      ui::set_tone(icon, tone, theme::INK_SOFT);
      resize(icon, iw, ui::line_height(Font::ICON_SMALL));
      place(icon, x, y + (line - ui::line_height(Font::ICON_SMALL)) / 2);
    }
    ui::set_text(words, text);
    ui::set_tone(words, tone, theme::INK_SOFT);
    resize(words, tw, ui::line_height(Font::BODY));
    place(words, x + w - tw, y + (line - ui::line_height(Font::BODY)) / 2);
    return w;
  }
  void on_theme() {
    if (icon) ui::set_color(icon, theme::INK_SOFT);
    if (words) ui::set_color(words, theme::INK_SOFT);
  }
};

static int chip_line() { return std::max(ui::line_height(Font::BODY), ui::line_height(Font::ICON_SMALL)); }

static uint32_t battery_icon(const Reading &r) {
  if (r.charging) return ICON_BATTERY_CHARGING;
  if (r.battery < 10) return ICON_BATTERY_ALERT;
  return ICON_BATTERY_LEVELS[std::max(0, std::min(9, r.battery / 10 - 1))];   // 10-19 % is battery-10, 100 % full
}

static Tone battery_tone(const Reading &r) {
  if (r.charging) return Tone::NORMAL;
  return r.battery < 10 ? Tone::ALERT : r.battery < 20 ? Tone::BUSY : Tone::NORMAL;
}

static std::string battery_words(const Toothbrush *p, const Reading &r) {
  return r.battery < 0 ? "" : tessera::format(p->text("percent"), r.battery);
}

static std::string pressure_words(const Toothbrush *p, const Reading &r) {
  switch (r.pressure) {
    case Pressure::TOO_HIGH: return p->text("pressure_high");
    case Pressure::TOO_LOW: return p->text("pressure_low");
    case Pressure::NORMAL: return p->text("pressure_ok");
    default: return "";
  }
}

static Tone pressure_tone(const Reading &r) {
  return r.pressure == Pressure::TOO_HIGH ? Tone::ALERT : r.pressure == Pressure::TOO_LOW ? Tone::BUSY : Tone::NORMAL;
}

// The words of a reading: in the ring's middle (big, then small) and beside it (the zone, then a line of detail).
struct Words {
  std::string big, small, zone, detail;
  Tone tone = Tone::NORMAL;   // of `small` and `zone`
  bool digits = false;        // `big` is a time
};

static Words words_of(const Toothbrush *p, const Reading &r) {
  Words w;
  switch (r.phase) {
    case Phase::BRUSHING: {
      w.big = clock_of(r.seconds);
      w.digits = true;
      w.small = w.zone = p->zone_name(r.zones, r.active);
      const std::string step = tessera::fill(tessera::fill(p->text("zone_of"), "i", std::to_string(r.active + 1)).c_str(),
                                             "n", std::to_string(r.zones));
      const int left = r.routine - r.seconds;
      w.detail = left > 0 ? tessera::fill(tessera::fill(p->text("joined"), "a", step).c_str(), "b",
                                          tessera::fill(p->text("left"), "t", clock_of(left)))
                          : step;
      break;
    }
    case Phase::RESULT:
      w.big = clock_of(r.seconds);
      w.digits = true;
      w.small = w.zone = p->text(r.complete ? "done" : "stopped");
      w.tone = r.complete ? Tone::GOOD : Tone::BUSY;
      w.detail = tessera::fill(tessera::fill(p->text("zones_done"), "i", std::to_string(r.done)).c_str(), "n",
                               std::to_string(r.zones));
      break;
    case Phase::IDLE:
      w.big = p->text("ready");
      w.zone = p->text("ready");
      w.small = w.detail = p->last_text();
      break;
    case Phase::OFFLINE:
      w.big = w.zone = p->text("away");
      w.small = w.detail = p->last_text();
      break;
    default:
      w.big = w.zone = p->text("waiting");
      w.detail = p->text("waiting_hint");
      break;
  }
  return w;
}

// ---- The tile ----

enum class Layout : uint8_t {
  SMALL,  // the ring alone, the time in its middle (1x1)
  WIDE,   // the ring on the left, the words on the right (2x1, 3x1)
  TALL,   // the ring on top, the words under it (1x2)
  LARGE,  // the name, a row of chips, and a large ring (2x2 and up)
};

class BrushTile : public tessera::Tile {
 public:
  explicit BrushTile(Toothbrush *plugin) : plugin_(plugin) {}

  void create(const tessera::TileContext &c) override {
    w_ = c.width;
    h_ = c.height;
    tile_ = c.tile;
    options_ = options_of(c.options);
    const char *name = c.name && *c.name ? c.name : plugin_->text("brush");
    snprintf(name_, sizeof name_, "%s", name);

    const int gap = ui::px(8);
    const int title = ui::line_height(Font::TITLE);
    layout_ = w_ >= h_ * 3 / 2 ? Layout::WIDE
              : h_ >= w_ * 3 / 2 ? Layout::TALL
              : std::min(w_, h_) >= ui::px(250) ? Layout::LARGE
                                                 : Layout::SMALL;
    const bool named = options_.show_name && layout_ != Layout::SMALL;
    if (named) {
      name_label_ = ui::label(c.parent, Font::TITLE, theme::INK);
      lv_obj_set_size(name_label_, w_, title);
      ui::set_text(name_label_, name_);
    }

    int cx = w_ / 2, cy = h_ / 2, d = std::min(w_, h_);
    switch (layout_) {
      case Layout::WIDE:
        d = h_;
        cx = cy = d / 2;
        column_x_ = d + gap;
        column_w_ = std::max(1, w_ - column_x_);
        if (name_label_) {
          lv_obj_set_size(name_label_, column_w_, title);
          lv_obj_set_pos(name_label_, column_x_, 0);
        }
        break;
      case Layout::TALL: {
        const int top = named ? title + gap : 0;
        const int under = ui::line_height(Font::BODY_LARGE) + ui::line_height(Font::BODY) + chip_line() + 2 * gap;
        d = std::max(ui::px(40), std::min(w_, h_ - top - under));
        cx = w_ / 2;
        cy = top + d / 2;
        column_x_ = 0;
        column_w_ = w_;
        column_y_ = top + d + gap;
        break;
      }
      case Layout::LARGE: {
        const int top = (named ? title + gap : 0) + chip_line() + gap;
        chips_y_ = named ? title + gap : 0;
        d = std::max(ui::px(40), std::min(w_, h_ - top));
        cx = w_ / 2;
        cy = top + (h_ - top) / 2;
        break;
      }
      default:
        break;
    }
    ring_.create(c.parent, cx, cy, d, plugin_, false);
    cx_ = cx;
    cy_ = cy;

    big_ = ui::label(c.parent, Font::VALUE, theme::INK);
    lv_obj_set_style_text_align(big_, LV_TEXT_ALIGN_CENTER, 0);
    if (layout_ == Layout::SMALL || layout_ == Layout::LARGE) {
      small_ = ui::label(c.parent, Font::BODY, theme::MUTED);
      lv_obj_set_style_text_align(small_, LV_TEXT_ALIGN_CENTER, 0);
    }
    if (layout_ == Layout::WIDE || layout_ == Layout::TALL) {
      zone_ = ui::label(c.parent, Font::BODY_LARGE, theme::INK);
      detail_ = ui::text(c.parent, Font::BODY, theme::MUTED);
    }
    if (layout_ != Layout::SMALL) {
      battery_.create(c.parent);
      pressure_.create(c.parent);
      if (layout_ == Layout::LARGE) mode_.create(c.parent);
    }
  }

  void on_tick(uint32_t) override { paint(); }

  void on_theme() override {
    if (name_label_) ui::set_color(name_label_, theme::INK);
    ui::set_color(big_, theme::INK);
    if (small_) ui::set_color(small_, theme::MUTED);
    if (zone_) ui::set_color(zone_, theme::INK);
    if (detail_) ui::set_color(detail_, theme::MUTED);
    battery_.on_theme();
    pressure_.on_theme();
    mode_.on_theme();
    ring_.on_theme();
    paint();
  }

  void on_tap() override {
    plugin_->card_options = options_;
    tessera::open_card(plugin_->plugin_id(), "zones", "", tile_);
  }

 private:
  void paint() {
    const Reading r = plugin_->read(options_);
    ring_.set_zones(r.zones);
    ring_.paint(r.active, r.done);
    const Words w = words_of(plugin_, r);

    // The middle of the ring: the time (or a word) as large as fits, and under it a line when there is room.
    const int inner = ring_.inner();
    const int room_w = inner * 80 / 100;
    const bool with_small = small_ && !w.small.empty() && inner >= ui::px(90);
    const int small_h = with_small ? ui::line_height(Font::BODY) : 0;
    const Font face = fit(w.big, room_w, inner * (with_small ? 45 : 60) / 100);
    ui::set_font(big_, face);
    ui::set_text(big_, w.big);
    ui::set_color(big_, r.phase == Phase::BRUSHING || r.phase == Phase::RESULT ? theme::INK : theme::MUTED);
    const int big_h = ui::line_height(face);
    const int top = cy_ - (big_h + small_h) / 2;
    resize(big_, room_w, big_h);
    place(big_, cx_ - room_w / 2, top);
    if (small_) {
      show(small_, with_small);
      if (with_small) {
        ui::set_text(small_, w.small);
        ui::set_tone(small_, w.tone, theme::MUTED);
        resize(small_, room_w, small_h);
        place(small_, cx_ - room_w / 2, top + big_h);
      }
    }

    // Beside or under the ring: the zone, a line of detail, and the chips.
    const int gap = ui::px(8);
    if (zone_) {
      int y = column_y_;
      if (layout_ == Layout::WIDE) {
        const int block = (name_label_ ? ui::line_height(Font::TITLE) + gap : 0) + ui::line_height(Font::BODY_LARGE) +
                          2 * ui::line_height(Font::BODY) + chip_line() + 2 * gap;
        y = std::max(0, (h_ - block) / 2) + (name_label_ ? ui::line_height(Font::TITLE) + gap : 0);
        if (name_label_) place(name_label_, column_x_, y - ui::line_height(Font::TITLE) - gap);
      }
      ui::set_text(zone_, w.zone);
      ui::set_tone(zone_, w.tone, theme::INK);
      resize(zone_, column_w_, ui::line_height(Font::BODY_LARGE));
      place(zone_, column_x_, y);
      y += ui::line_height(Font::BODY_LARGE);
      const int lines = layout_ == Layout::WIDE ? 2 : 1;
      ui::set_text(detail_, w.detail);
      resize(detail_, column_w_, lines * ui::line_height(Font::BODY));
      place(detail_, column_x_, y);
      y += lines * ui::line_height(Font::BODY) + gap;
      chips(r, column_x_, y, column_w_);
    } else if (layout_ == Layout::LARGE) {
      chips(r, 0, chips_y_, w_);
    }
  }

  void chips(const Reading &r, int x, int y, int room) {
    const int gap = ui::px(12);
    int used = battery_.put(x, y, room, battery_icon(r), battery_words(plugin_, r), battery_tone(r));
    if (used) used += gap;
    const int p = pressure_.put(x + used, y, room - used, ICON_GAUGE, pressure_words(plugin_, r), pressure_tone(r));
    if (p) used += p + gap;
    if (mode_.words) mode_.put(x + used, y, room - used, 0, plugin_->mode_text(), Tone::NORMAL);
  }

  Toothbrush *plugin_;
  Options options_;
  char name_[48] = "";
  Layout layout_ = Layout::SMALL;
  int w_ = 0, h_ = 0, tile_ = -1, cx_ = 0, cy_ = 0;
  int column_x_ = 0, column_y_ = 0, column_w_ = 0, chips_y_ = 0;
  Ring ring_;
  lv_obj_t *name_label_{}, *big_{}, *small_{}, *zone_{}, *detail_{};
  Chip battery_, pressure_, mode_;
};

// ---- The card: the whole session over the page ----

class ZonesCard : public tessera::Card {
 public:
  explicit ZonesCard(Toothbrush *plugin) : plugin_(plugin) {}

  void open(const tessera::CardContext &c) override {
    options_ = plugin_->card_options;
    const int w = c.width, h = c.height, gap = ui::px(ui::large() ? 24 : 12);
    const bool across = w >= h * 6 / 5;
    int d, cx, cy, x0, y0, cw;
    if (across) {
      d = std::min(h, w * 55 / 100);
      cx = d / 2;
      cy = h / 2;
      x0 = d + gap;
      cw = std::max(1, w - x0);
      y0 = 0;
    } else {
      d = std::min(w, h * 55 / 100);
      cx = w / 2;
      cy = d / 2;
      x0 = 0;
      cw = w;
      y0 = d + gap;
    }
    ring_.create(c.parent, cx, cy, d, plugin_, true);
    cx_ = cx;
    cy_ = cy;
    big_ = ui::label(c.parent, Font::VALUE, theme::INK);
    lv_obj_set_style_text_align(big_, LV_TEXT_ALIGN_CENTER, 0);
    small_ = ui::label(c.parent, Font::BODY, theme::MUTED);
    lv_obj_set_style_text_align(small_, LV_TEXT_ALIGN_CENTER, 0);

    // The column: the zone, a line of detail, and the readings in blocks of two columns, as many rows as fit.
    const int zone_h = ui::line_height(Font::HEADLINE), detail_h = ui::line_height(Font::BODY);
    const int pad = ui::px(ui::large() ? 10 : 6), cell_gap = ui::px(8);
    const int cell_h = 2 * pad + ui::line_height(Font::BODY) + ui::line_height(Font::BODY_LARGE);
    const int room = (across ? h : h - y0) - zone_h - detail_h - gap;
    const int rows = std::max(0, std::min(3, (room + cell_gap) / (cell_h + cell_gap)));
    const int column_h = zone_h + detail_h + (rows ? gap + rows * cell_h + (rows - 1) * cell_gap : 0);
    int y = across ? std::max(0, (h - column_h) / 2) : y0;
    zone_ = ui::label(c.parent, Font::HEADLINE, theme::INK);
    lv_obj_set_size(zone_, cw, zone_h);
    lv_obj_set_pos(zone_, x0, y);
    y += zone_h;
    detail_ = ui::label(c.parent, Font::BODY, theme::MUTED);
    lv_obj_set_size(detail_, cw, detail_h);
    lv_obj_set_pos(detail_, x0, y);
    y += detail_h + gap;
    const int cell_w = (cw - cell_gap) / 2;
    static const char *CAPTIONS[] = {"battery", "pressure", "mode", "head", "last"};
    for (int i = 0; i < CELLS && i / 2 < rows; ++i) {
      lv_obj_t *cell = ui::block(c.parent, theme::CARD);
      lv_obj_set_size(cell, cell_w, cell_h);
      lv_obj_set_pos(cell, x0 + (i % 2) * (cell_w + cell_gap), y + (i / 2) * (cell_h + cell_gap));
      captions_[i] = ui::label(cell, Font::BODY, theme::SUBTLE);
      lv_obj_set_size(captions_[i], cell_w - 2 * pad, ui::line_height(Font::BODY));
      lv_obj_set_pos(captions_[i], pad, pad);
      ui::set_text(captions_[i], plugin_->text(CAPTIONS[i]));
      values_[i] = ui::label(cell, Font::BODY_LARGE, theme::INK);
      lv_obj_set_size(values_[i], cell_w - 2 * pad, ui::line_height(Font::BODY_LARGE));
      lv_obj_set_pos(values_[i], pad, pad + ui::line_height(Font::BODY));
    }
  }

  void on_tick(uint32_t) override { paint(); }

  void on_theme() override {
    ui::set_color(big_, theme::INK);
    ui::set_color(small_, theme::MUTED);
    ui::set_color(zone_, theme::INK);
    ui::set_color(detail_, theme::MUTED);
    for (int i = 0; i < CELLS; ++i) {
      if (captions_[i]) ui::set_color(captions_[i], theme::SUBTLE);
      if (values_[i]) ui::set_color(values_[i], theme::INK);
    }
    ring_.on_theme();
    paint();
  }

 private:
  static constexpr int CELLS = 5;

  void paint() {
    const Reading r = plugin_->read(options_);
    ring_.set_zones(r.zones);
    ring_.paint(r.active, r.done);
    const Words w = words_of(plugin_, r);

    const int inner = ring_.inner(), room_w = inner * 80 / 100;
    const int small_h = ui::line_height(Font::BODY);
    const Font face = fit(w.big, room_w, inner * 45 / 100);
    ui::set_font(big_, face);
    ui::set_text(big_, w.big);
    ui::set_color(big_, r.phase == Phase::BRUSHING || r.phase == Phase::RESULT ? theme::INK : theme::MUTED);
    const int big_h = ui::line_height(face), top = cy_ - (big_h + small_h) / 2;
    resize(big_, room_w, big_h);
    place(big_, cx_ - room_w / 2, top);
    const std::string under =
        r.phase == Phase::BRUSHING ? tessera::fill(plugin_->text("of"), "t", clock_of(r.routine)) : "";
    ui::set_text(small_, under);
    resize(small_, room_w, small_h);
    place(small_, cx_ - room_w / 2, top + big_h);

    ui::set_text(zone_, w.zone);
    ui::set_tone(zone_, w.tone, theme::INK);
    ui::set_text(detail_, w.detail);

    bool worn = false;
    const std::string head = plugin_->head_text(options_, &worn);
    const std::string none = plugin_->text("none");
    const std::string battery = battery_words(plugin_, r);
    const std::string pressure = pressure_words(plugin_, r);
    const std::string mode = plugin_->mode_text();
    const std::string last = plugin_->last_text();
    const std::string *texts[CELLS] = {&battery, &pressure, &mode, &head, &last};
    const Tone tones[CELLS] = {battery_tone(r), pressure_tone(r), Tone::NORMAL, worn ? Tone::BUSY : Tone::NORMAL,
                               Tone::NORMAL};
    for (int i = 0; i < CELLS; ++i) {
      if (!values_[i]) continue;
      ui::set_text(values_[i], texts[i]->empty() ? none : *texts[i]);
      ui::set_tone(values_[i], texts[i]->empty() ? Tone::NORMAL : tones[i], texts[i]->empty() ? theme::SUBTLE : theme::INK);
    }
  }

  Toothbrush *plugin_;
  Options options_;
  Ring ring_;
  int cx_ = 0, cy_ = 0;
  lv_obj_t *big_{}, *small_{}, *zone_{}, *detail_{};
  lv_obj_t *captions_[CELLS]{}, *values_[CELLS]{};
};

void Toothbrush::setup() {
  subscribe();
  add_tile("brush", [this]() { return new BrushTile(this); });
  add_card("zones", [this]() { return new ZonesCard(this); }, true);
}

}  // namespace esphome::toothbrush
