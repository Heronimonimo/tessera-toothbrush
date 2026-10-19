// The brush's states and its session: what the app sends of the brush's entities, and what the plugin makes of it.
#include "toothbrush.h"
#include "esphome/core/hal.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace esphome::toothbrush {

// The tile's options of kind entity, in the order of Role.
static const char *const ROLE_OPTIONS[ROLES] = {"brush", "sector", "time", "battery", "pressure", "mode", "head", "last"};

void Snapshot::read(JsonObjectConst data) {
  too_large = strcmp(data["wait"] | "", "too_large") == 0;
  JsonObjectConst entities = data["entities"];
  for (int r = 0; r < ROLES; ++r) {
    JsonObjectConst entity = entities[ROLE_OPTIONS[r]];
    heard[r] = !entity.isNull();
    snprintf(state[r], sizeof state[r], "%s", heard[r] ? (entity["state"] | "") : "");
  }
}

// ---- Reading the brush's states ----
// Every brush reports in its own words; these follow the toothbrush card (toothbrush-card.js, render()).

static std::string lower(const std::string &text) {
  std::string low = text;
  std::transform(low.begin(), low.end(), low.begin(), [](unsigned char c) { return std::tolower(c); });
  return low;
}

// No value: nothing from Home Assistant yet, or an entity that is unknown or out of reach.
static bool missing(const std::string &state) {
  if (state.empty()) return true;
  const std::string low = lower(state);
  return low == "unknown" || low == "unavailable" || low == "none";
}

static bool number_of(const std::string &state, float *out) {
  if (missing(state)) return false;
  char *end = nullptr;
  const float v = std::strtof(state.c_str(), &end);
  if (end == state.c_str() || !std::isfinite(v)) return false;
  *out = v;
  return true;
}

// A session runs: "running" (Oral-B, Sonicare), "Running" (Laifen), "on" (Xiaomi's binary main entity).
static bool brushing(const std::string &state) {
  const std::string low = lower(state);
  return low == "running" || low == "run" || low == "on" || low == "brushing";
}

// The zone a sector entity names, 0-based: "sector_3" and "3" are 2; -1 for none ("success", "no_sector").
static int sector_of(const std::string &state) {
  if (missing(state)) return -1;
  for (const char *p = state.c_str(); *p; ++p)
    if (std::isdigit(static_cast<unsigned char>(*p))) {
      const int n = std::atoi(p);
      return n >= 1 && n <= 9 ? n - 1 : -1;
    }
  return -1;
}

// Days since 1970 of a civil date (Howard Hinnant's days_from_civil).
static int32_t civil_days(int year, unsigned month, unsigned day) {
  year -= month <= 2;
  const int era = (year >= 0 ? year : year - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(year - era * 400);
  const unsigned doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
  return era * 146097 + static_cast<int32_t>(yoe * 365 + yoe / 4 - yoe / 100 + doy) - 719468;
}

// A moment as Home Assistant writes a timestamp sensor's state, "2026-10-18T05:12:00+00:00" (or with a space, or Z),
// as seconds since 1970; 0 when it is none.
static uint32_t moment_of(const std::string &text) {
  int y = 0, mo = 0, d = 0, h = 0, mi = 0, s = 0;
  if (text.size() < 19 || sscanf(text.c_str(), "%4d-%2d-%2d%*c%2d:%2d:%2d", &y, &mo, &d, &h, &mi, &s) != 6) return 0;
  if (mo < 1 || mo > 12 || d < 1 || d > 31) return 0;
  long offset = 0;
  const size_t zone = text.find_first_of("+-", 19);
  if (zone != std::string::npos) {
    int oh = 0, om = 0;
    if (sscanf(text.c_str() + zone + 1, "%2d:%2d", &oh, &om) >= 1)
      offset = (oh * 3600L + om * 60L) * (text[zone] == '-' ? -1 : 1);
  }
  const long long epoch = civil_days(y, mo, d) * 86400LL + h * 3600LL + mi * 60LL + s - offset;
  return epoch > 0 ? static_cast<uint32_t>(epoch) : 0;
}

std::string clock_of(int seconds) {
  char text[12];
  seconds = std::max(0, seconds);
  snprintf(text, sizeof text, "%d:%02d", seconds / 60, seconds % 60);
  return text;
}

static int since(uint32_t now, uint32_t then) { return static_cast<int>((now - then) / 1000u); }

// ---- The session ----

int Session::elapsed(uint32_t now) const {
  // The brush's own count once it reported in this session (with the seconds since, up to two, so the time runs on
  // smoothly between its reports); before that, and for a brush without one (Xiaomi), the time since it began.
  if (time_value >= 0 && static_cast<int32_t>(time_ms - started_ms) >= 0)
    return time_value + std::min(2, since(now, time_ms));
  return since(now, started_ms);
}

void Session::follow(const Snapshot &snap, uint32_t now) {
  const std::string state = snap.value(STATE);
  const bool runs = !missing(state) && brushing(state);
  float t = 0;
  const int value = number_of(snap.value(TIME), &t) ? static_cast<int>(std::lround(t)) : -1;
  if (value != time_value) {
    time_value = value;
    time_ms = now;
  }
  const int zone = sector_of(snap.value(SECTOR));
  if (zone >= 4) decodes_six = true;   // a fifth or sixth sector: the integration names all six itself
  const bool fresh_sector = zone != raw;
  if (fresh_sector) raw = zone;

  if (runs && !active) {
    // A session begins: from scratch, never from the last one's peak.
    started_ms = now;
    sector_seen = false;
    highest = last_raw = corrected = seen_raw = -1;
    elapsed_s = 0;
    result = false;
  }
  if (runs) {
    // A sector left over from the last session counts only once it changes, or when it is the first zone.
    if (zone >= 0 && (fresh_sector || sector_seen || zone == 0)) {
      sector_seen = true;
      seen_raw = zone;
      // The wrap correction of sector-state.js: the oralb mapping before Home Assistant 2026.8 had no sectors 5 and 6
      // and wrapped back instead, so while brushing a reading that does not pass the highest one is the next zone.
      // The same reading again is the handle repeating itself, not progress.
      if (zone != last_raw) {
        last_raw = zone;
        highest = zone > highest ? zone : std::min(highest + 1, 5);
        corrected = highest;
      }
    }
    elapsed_s = elapsed(now);
  } else if (active) {
    // The session ended: keep what it did.
    result = true;
    result_epoch = tessera::epoch();
    result_ms = now;
    result_seconds = elapsed_s;
    result_raw = seen_raw;
    result_corrected = corrected;
    result_sector_seen = sector_seen;
  }
  active = runs;
}

int Session::zone_index(int zones, int routine, int seconds, bool seen, int raw_zone, int corrected_zone) const {
  if (seen) {
    // Six zones from a brush that never named a fifth: the corrected count; otherwise the brush's own word.
    const int index = zones == 6 && !decodes_six ? corrected_zone : raw_zone;
    return std::max(0, std::min(index, zones - 1));
  }
  // No sector entity (Xiaomi, Laifen): the zones go by time, evenly over the routine.
  return std::min(zones - 1, std::max(0, seconds) * zones / std::max(1, routine));
}

// ---- The plugin ----

Session *Toothbrush::session(const Options &o) {
  if (!o.brush[0]) return nullptr;
  for (Session &s : sessions_)
    if (!strcmp(s.brush, o.brush)) return &s;
  Session *free = nullptr;
  for (Session &s : sessions_)
    if (!s.brush[0]) {
      free = &s;
      break;
    }
  if (!free) {
    free = &sessions_[next_];
    next_ = (next_ + 1) % SESSIONS;
  }
  *free = Session();
  snprintf(free->brush, sizeof free->brush, "%s", o.brush);
  return free;
}

Reading Toothbrush::read(const Options &o, const Snapshot &snap) {
  Reading r;
  r.routine = std::max(1, o.routine);
  Session *s = session(o);
  if (!s) return r;   // CHOOSE
  r.zones = !strcmp(o.zones, "6") ? 6 : !strcmp(o.zones, "4") ? 4 : s->decodes_six ? 6 : 4;

  const std::string state = snap.value(STATE);
  float v = 0;
  if (number_of(snap.value(BATTERY), &v)) r.battery = std::max(0, std::min(100, static_cast<int>(std::lround(v))));
  if (!missing(state)) r.charging = lower(state).find("charg") != std::string::npos;
  const std::string pressure = snap.value(PRESSURE);
  if (!missing(pressure)) {
    const std::string p = lower(pressure);
    if (p == "on" || p.find("high") != std::string::npos) r.pressure = Pressure::TOO_HIGH;
    else if (p.find("low") != std::string::npos) r.pressure = Pressure::TOO_LOW;
    else r.pressure = Pressure::NORMAL;   // off, ok, optimal, normal
  }

  const uint32_t now = millis();
  if (s->active) {
    r.phase = Phase::BRUSHING;
    r.seconds = s->elapsed(now);
    r.active = s->zone_index(r.zones, r.routine, r.seconds, s->sector_seen, s->seen_raw, s->corrected);
    r.done = r.active;
    return r;
  }
  if (s->result) {
    const uint32_t clock = tessera::epoch();
    const bool held = o.hold <= 0 ||
                      (s->result_epoch && clock ? clock - s->result_epoch < static_cast<uint32_t>(o.hold) * 3600u
                                                : since(now, s->result_ms) < o.hold * 3600);
    if (held) {
      r.phase = Phase::RESULT;
      r.seconds = s->result_seconds;
      r.complete = s->result_seconds >= r.routine - 5;
      // The zones before the one it stopped in, or as far as the time went, whichever is further.
      const int by_zone = s->result_sector_seen ? s->zone_index(r.zones, r.routine, s->result_seconds, true,
                                                                s->result_raw, s->result_corrected)
                                                : 0;
      const int by_time = s->result_seconds * r.zones / r.routine;
      r.done = r.complete ? r.zones : std::min(r.zones, std::max(by_zone, by_time));
      return r;
    }
  }
  const std::string low = lower(state);
  r.phase = state.empty() || low == "unknown" ? Phase::WAITING : low == "unavailable" ? Phase::OFFLINE : Phase::IDLE;
  return r;
}

std::string Toothbrush::mode_text(const Snapshot &snap) const {
  std::string text = snap.value(MODE);
  if (missing(text)) return "";
  // "daily_clean" as "Daily clean": the integration's own word, readable.
  std::replace(text.begin(), text.end(), '_', ' ');
  text[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(text[0])));
  return text;
}

std::string Toothbrush::head_text(const Snapshot &snap, const Options &o, bool *worn) const {
  *worn = false;
  const std::string head = snap.value(HEAD);
  if (missing(head)) return "";
  float v = 0;
  if (!number_of(head, &v)) return head;
  const long n = std::lround(v);
  if (!strcmp(o.head, "percent")) {
    *worn = n <= 10;
    return tessera::format(text("head_percent_n"), n);
  }
  if (!strcmp(o.head, "sessions")) {
    *worn = n <= 5;
    return tessera::format(text("head_sessions_n"), n);
  }
  *worn = n <= 7;
  return tessera::format(text("head_days_n"), n);
}

std::string Toothbrush::last_text(const Snapshot &snap) const {
  const std::string last = snap.value(LAST);
  if (missing(last)) return "";
  const uint32_t at = moment_of(last);
  if (!at) return last;   // not a moment (Xiaomi's score): as the brush says it
  const int32_t days = tessera::days_from_today(at);
  if (days == 0) return tessera::fill(text("today_at"), "time", tessera::clock_text(at));
  if (days == -1) return tessera::fill(text("yesterday_at"), "time", tessera::clock_text(at));
  return tessera::fill(tessera::fill(text("day_at"), "day", tessera::date_text(at)).c_str(), "time",
                       tessera::clock_text(at));
}

const char *Toothbrush::zone_name(int zones, int index) const {
  static const char *FOUR[] = {"zone_ll", "zone_lr", "zone_ul", "zone_ur"};
  static const char *SIX[] = {"zone_ll", "zone_lf", "zone_lr", "zone_ur", "zone_uf", "zone_ul"};
  if (index < 0 || index >= zones) return "";
  return text(zones == 6 ? SIX[index] : FOUR[index]);
}

const char *Toothbrush::side_name(int zones, int index) const {
  static const char *FOUR[] = {"side_left", "side_right", "side_left", "side_right"};
  static const char *SIX[] = {"side_left", "side_front", "side_right", "side_right", "side_front", "side_left"};
  if (index < 0 || index >= zones) return "";
  return text(zones == 6 ? SIX[index] : FOUR[index]);
}

}  // namespace esphome::toothbrush
