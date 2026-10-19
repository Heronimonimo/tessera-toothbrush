#pragma once
#include "esphome/core/component.h"
#include "esphome/components/smart_display/plugin_api.h"
#include <string>

namespace esphome::toothbrush {

// Where a brush stands now, for one tile's options: what its ring and its words show.
enum class Phase : uint8_t {
  CHOOSE,    // no toothbrush chosen in the tile's options yet
  WAITING,   // chosen, but no state from Home Assistant yet
  OFFLINE,   // the brush is out of reach (unavailable)
  IDLE,      // nothing to show but the readings
  BRUSHING,  // a session runs
  RESULT,    // a session ended, and its result is still held
};
enum class Pressure : uint8_t { UNKNOWN, NORMAL, TOO_HIGH, TOO_LOW };

struct Reading {
  Phase phase = Phase::CHOOSE;
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

// What the plugin reads of the brush: one option of kind entity each, by these ids (the manifest's tile options).
enum Role : uint8_t { STATE, SECTOR, TIME, BATTERY, PRESSURE, MODE, HEAD, LAST, ROLES };

// The options of a tile, as the card needs them too.
struct Options {
  char brush[80] = "";      // the toothbrush entity chosen, "" for none
  char zones[8] = "auto";   // auto, 4 or 6
  int routine = 120;        // seconds
  int hold = 2;             // hours a result stays; 0 until the next session
  char head[12] = "days";   // days, percent or sessions
};

// The brush's entities as the app last sent them with the tile's data (`entities`, plugin API 0.15). Fixed fields: a
// tile and a card are made in PSRAM, so this is there too.
struct Snapshot {
  char state[ROLES][48] = {};
  bool heard[ROLES] = {};
  bool too_large = false;   // the app could not send them in the tile's 2.6 KB
  void read(JsonObjectConst data);
  // A role's state, "" when its option was left empty or nothing came yet.
  std::string value(Role role) const { return heard[role] ? state[role] : ""; }
};

// A brush's session, followed from the states the app sends: kept by the plugin, by toothbrush entity, so a tile made
// again (a page coming back on the CYD), another tile of the same brush and the card all show the same session.
struct Session {
  char brush[80] = "";
  bool active = false;          // a session runs
  uint32_t started_ms = 0;      // when it began, by millis()
  int time_value = -1;          // the last reading of the time entity, -1 for none
  uint32_t time_ms = 0;         // when that reading came
  bool sector_seen = false;     // the sector entity reported a zone in this session
  bool decodes_six = false;     // the brush has reported a sector above 4: it names all six itself
  int raw = -1;                 // the last zone the brush reported, 0-based (-1 while it names none)
  int seen_raw = -1;            // the last zone it reported in this session, kept while it names none
  int highest = -1, last_raw = -1, corrected = -1;   // the wrap correction (sector-state.js)
  int elapsed_s = 0;            // seconds brushed, as last followed
  bool result = false;          // a session ended and its result is kept
  uint32_t result_epoch = 0;    // when it ended, on the screen's clock (0 when the clock was not set)
  uint32_t result_ms = 0;       // the same by millis()
  int result_seconds = 0;
  int result_raw = -1, result_corrected = -1;
  bool result_sector_seen = false;

  // Take in the states of a snapshot: a session that begins, a zone, the time, a session that ends.
  void follow(const Snapshot &snap, uint32_t now_ms);
  int elapsed(uint32_t now_ms) const;
  int zone_index(int zones, int routine, int seconds, bool seen, int raw_zone, int corrected_zone) const;
};

// "1:12": minutes and seconds.
std::string clock_of(int seconds);

// The plugin: a tile ("brush") and a card ("zones"). A tile's options name the brush's entities (kind entity, plugin
// API 0.15): the app sends their states with the tile's data and follows them, and another choice needs no build.
class Toothbrush : public Component, public tessera::Plugin {
 public:
  void setup() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  // The session of a tile's brush, made the first time a brush is seen (the oldest of four gives way).
  Session *session(const Options &options);
  // Where the brush stands for a tile with these options and these states.
  Reading read(const Options &options, const Snapshot &snap);
  // The words of the readings, "" for none: the mode ("Daily clean"), the brush head ("41 days") and whether it is
  // nearly worn, and the last session ("Today 07:12").
  std::string mode_text(const Snapshot &snap) const;
  std::string head_text(const Snapshot &snap, const Options &options, bool *worn) const;
  std::string last_text(const Snapshot &snap) const;
  // The name of a zone in brushing order ("Lower left"), from the screen's texts.
  const char *zone_name(int zones, int index) const;
  // Its side alone ("Left", "Front"), for the names around the ring, whose half already says which jaw.
  const char *side_name(int zones, int index) const;

  // The options of the tile the card was opened from (a card gets no options of its own).
  Options card_options;

 protected:
  static constexpr int SESSIONS = 4;
  Session sessions_[SESSIONS];
  uint8_t next_ = 0;   // the slot a new brush takes when all are in use
};

}  // namespace esphome::toothbrush
