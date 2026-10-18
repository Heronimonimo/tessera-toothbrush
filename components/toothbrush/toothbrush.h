#pragma once
#include "esphome/core/component.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/smart_display/plugin_api.h"
#include <string>

namespace esphome::toothbrush {

// Where a brush stands now, for one tile's options: what its ring and its words show.
enum class Phase : uint8_t {
  WAITING,   // no state from Home Assistant yet (or the state input points at nothing)
  OFFLINE,   // the brush is out of reach (unavailable)
  IDLE,      // nothing to show but the readings
  BRUSHING,  // a session runs
  RESULT,    // a session ended, and its result is still held
};
enum class Pressure : uint8_t { UNKNOWN, NORMAL, TOO_HIGH, TOO_LOW };

struct Reading {
  Phase phase = Phase::WAITING;
  int zones = 4;         // 4 or 6
  int active = -1;       // the zone brushed now, in brushing order (0 is the first); -1 for none
  int done = 0;          // how many zones, in brushing order, are done
  int seconds = 0;       // brushed so far, or in the session that ended
  int routine = 120;     // the session's length in seconds
  bool complete = false; // the session that ended took the whole routine
  int battery = -1;      // percent, -1 for none
  bool charging = false;
  Pressure pressure = Pressure::UNKNOWN;
};

// The options of a tile, as the card needs them too.
struct Options {
  char zones[8] = "auto";   // auto, 4 or 6
  int routine = 120;        // seconds
  int hold = 2;             // hours a result stays; 0 until the next session
  char head[12] = "days";   // days, percent or sessions
  bool show_name = true;
};

// The plugin: a tile ("brush") and a card ("zones"). It follows the session itself, from the brush's eight entities,
// so the tile and the card only paint what it knows, and a tile made again (a page coming back on the CYD) picks up a
// session where it stands.
class Toothbrush : public Component, public tessera::Plugin {
 public:
  void setup() override;
  float get_setup_priority() const override { return setup_priority::DATA; }
  void on_interval(uint32_t now_ms) override;

  void set_state(text_sensor::TextSensor *s) { state_ = s; }
  void set_sector(text_sensor::TextSensor *s) { sector_ = s; }
  void set_time(text_sensor::TextSensor *s) { time_ = s; }
  void set_battery(text_sensor::TextSensor *s) { battery_ = s; }
  void set_pressure(text_sensor::TextSensor *s) { pressure_ = s; }
  void set_mode(text_sensor::TextSensor *s) { mode_ = s; }
  void set_head(text_sensor::TextSensor *s) { head_ = s; }
  void set_last(text_sensor::TextSensor *s) { last_ = s; }

  // Where the brush stands for a tile with these options.
  Reading read(const Options &options) const;
  // The words of the readings, "" for none: the mode ("Daily clean"), the brush head ("41 days") and whether it is
  // nearly worn, and the last session ("Today 07:12").
  std::string mode_text() const;
  std::string head_text(const Options &options, bool *worn) const;
  std::string last_text() const;
  // The name of a zone in brushing order ("Lower left"), from the screen's texts.
  const char *zone_name(int zones, int index) const;

  // The options of the tile the card was opened from (a card gets no options of its own).
  Options card_options;

 protected:
  text_sensor::TextSensor *state_{}, *sector_{}, *time_{}, *battery_{}, *pressure_{}, *mode_{}, *head_{}, *last_{};

  // The session, followed every 250 ms.
  bool active_ = false;          // a session runs
  uint32_t started_ms_ = 0;      // when it began, by millis()
  int time_value_ = -1;          // the last reading of the time entity, -1 for none
  uint32_t time_ms_ = 0;         // when that reading came
  bool sector_seen_ = false;     // the sector entity reported a zone in this session
  bool decodes_six_ = false;     // the brush has reported a sector above 4: it names all six itself
  int raw_ = -1;                 // the last zone the brush reported, 0-based (-1 while it names none)
  int seen_raw_ = -1;            // the last zone it reported in this session, kept while it names none
  int highest_ = -1, last_raw_ = -1, corrected_ = -1;   // the wrap correction (sector-state.js)
  int elapsed_ = 0;              // seconds brushed, as last followed

  bool result_ = false;          // a session ended and its result is kept
  uint32_t result_epoch_ = 0;    // when it ended, on the screen's clock (0 when the clock was not set)
  uint32_t result_ms_ = 0;       // the same by millis()
  int result_seconds_ = 0;
  int result_raw_ = -1, result_corrected_ = -1;
  bool result_sector_seen_ = false;

  int elapsed(uint32_t now_ms) const;
  int zone_index(int zones, int routine, int seconds, bool sector_seen, int raw, int corrected) const;
};

}  // namespace esphome::toothbrush
