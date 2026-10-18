// The brush's entities and the session: what the screen reads from Home Assistant, and what it makes of it.
#include "toothbrush.h"
#include "esphome/components/api/api_server.h"
#include "esphome/core/application.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include "esphome/core/string_ref.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace esphome::toothbrush {

static const char *const TAG = "toothbrush";

// ---- Finding the brush's entities ----
// A screen cannot ask Home Assistant which entities belong to a device, so the plugin finds them by the names their
// integration gives them: the toothbrush input names the main entity, its name without the integration's word for it
// is the device's part of every entity of that brush, and each reading is that part with its own word. These are the
// English entity ids (an installation in another language names them in its own words: then the inputs name them).
// The words follow the translation keys the toothbrush card reads (toothbrush-card.js, findDeviceEntities).
struct Brand {
  const char *main;              // how the main entity's id ends
  const char *roles[ROLES];      // each reading's entity, with %s for the device's part; nullptr where it has none
  const char *mode_select;       // a select of the mode beside the sensor, nullptr for none
};

static const Brand BRANDS[] = {
    // Oral-B, built in (oralb) and Oral-B Live (oralb_live, the same keys plus the brush head and the last session).
    {"_toothbrush_state",
     {nullptr, "sensor.%s_sector", "sensor.%s_time", "sensor.%s_battery", "sensor.%s_pressure", "sensor.%s_mode",
      "sensor.%s_refill_days", "sensor.%s_last_session"},
     nullptr},
    // Philips Sonicare (philips_sonicare_ble). Its brush head is a device of its own: the input names it.
    {"_handle_state",
     {nullptr, "sensor.%s_sector", "sensor.%s_brushing_time", "sensor.%s_battery", "sensor.%s_pressure_state",
      "sensor.%s_brushing_mode", nullptr, "sensor.%s_last_session"},
     nullptr},
    // Laifen (laifen_ble): no sectors, so the zones go by time.
    {"_status",
     {nullptr, nullptr, "sensor.%s_timer", "sensor.%s_battery", "binary_sensor.%s_pressing_too_hard",
      "sensor.%s_mode", nullptr, nullptr},
     "select.%s_mode"},
    // Xiaomi (xiaomi_ble): no live time and no sectors; its consumable is the percent left, its score the last result.
    {"_toothbrush",
     {nullptr, nullptr, nullptr, "sensor.%s_battery", nullptr, nullptr, "sensor.%s_consumable", "sensor.%s_score"},
     nullptr},
};

static bool ends_with(const char *text, const char *end) {
  const size_t n = strlen(text), m = strlen(end);
  return n > m && !strcmp(text + n - m, end);
}

void Toothbrush::set_entity(uint8_t role, const char *entity_id) {
  if (role >= ROLES || !entity_id) return;
  snprintf(sources_[role].entity, sizeof sources_[role].entity, "%s", entity_id);
  chosen_[role] = *entity_id != '\0';
}

void Toothbrush::find_siblings() {
  const char *dot = strchr(sources_[STATE].entity, '.');
  if (!dot) return;
  const char *object = dot + 1;
  for (const Brand &brand : BRANDS) {
    if (!ends_with(object, brand.main)) continue;
    char device[80];
    snprintf(device, sizeof device, "%.*s", static_cast<int>(strlen(object) - strlen(brand.main)), object);
    for (int r = SECTOR; r < ROLES; ++r)
      if (!chosen_[r] && brand.roles[r]) snprintf(sources_[r].entity, sizeof sources_[r].entity, brand.roles[r], device);
    if (!chosen_[MODE] && brand.mode_select)
      snprintf(mode_select_.entity, sizeof mode_select_.entity, brand.mode_select, device);
    return;
  }
  ESP_LOGW(TAG, "'%s' is no main entity of a toothbrush integration it knows: only the inputs that name an entity "
                "are read", sources_[STATE].entity);
}

static void follow(char *entity, char *state, size_t room, bool *heard) {
  if (!*entity) return;
  // The same subscription a homeassistant text sensor makes: Home Assistant sends the state now and at every change.
  api::global_api_server->subscribe_home_assistant_state(entity, nullptr, [state, room, heard](StringRef value) {
    snprintf(state, room, "%.*s", static_cast<int>(value.size()), value.c_str());
    *heard = true;
  });
}

std::string Toothbrush::entity_of(const text::Text *setting) {
  if (!setting || !setting->has_state()) return "";
  std::string id = setting->state;
  const size_t from = id.find_first_not_of(" \t"), to = id.find_last_not_of(" \t");
  id = from == std::string::npos ? "" : id.substr(from, to - from + 1);
  const size_t dot = id.find('.');
  if (dot == std::string::npos || dot == 0 || dot + 1 >= id.size() || id.size() >= sizeof(Source::entity)) return "";
  for (char c : id)
    if (!(std::islower(static_cast<unsigned char>(c)) || std::isdigit(static_cast<unsigned char>(c)) || c == '_' ||
          c == '.'))
      return "";
  return id;
}

void Toothbrush::subscribe() {
  // The settings were restored before this (a template text sets itself up first, at the priority of hardware).
  for (int r = SECTOR; r < ROLES; ++r) {
    const std::string id = entity_of(overrides_[r]);
    if (!id.empty()) set_entity(r, id.c_str());
  }
  find_siblings();
  static const char *const NAMES[] = {"state", "sector", "time", "battery", "pressure", "mode", "head", "last"};
  for (int r = 0; r < ROLES; ++r) {
    Source &s = sources_[r];
    if (*s.entity) ESP_LOGCONFIG(TAG, "  %s: %s%s", NAMES[r], s.entity, chosen_[r] ? "" : " (found)");
    follow(s.entity, s.state, sizeof s.state, &s.heard);
  }
  if (*mode_select_.entity) ESP_LOGCONFIG(TAG, "  mode: %s (found)", mode_select_.entity);
  follow(mode_select_.entity, mode_select_.state, sizeof mode_select_.state, &mode_select_.heard);
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

std::string Toothbrush::value(Role role) const {
  if (role == MODE && mode_select_.heard && !missing(mode_select_.state)) return mode_select_.state;
  const Source &s = sources_[role];
  return s.heard ? s.state : "";
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

// ---- The session, followed every 250 ms ----

int Toothbrush::elapsed(uint32_t now) const {
  // The brush's own count once it reported in this session (with the seconds since, up to two, so the time runs on
  // smoothly between its reports); before that, and for a brush without one (Xiaomi), the time since it began.
  if (time_value_ >= 0 && static_cast<int32_t>(time_ms_ - started_ms_) >= 0)
    return time_value_ + std::min(2, since(now, time_ms_));
  return since(now, started_ms_);
}

void Toothbrush::on_interval(uint32_t now) {
  // A setting that names another entity than the one the screen follows: restart once, so it tells Home Assistant.
  if (!restarting_)
    for (int r = SECTOR; r < ROLES; ++r) {
      const std::string id = entity_of(overrides_[r]);
      if (id != (chosen_[r] ? sources_[r].entity : "")) {
        ESP_LOGI(TAG, "A toothbrush setting changed: restarting to follow %s", id.empty() ? "the entity found" : id.c_str());
        restarting_ = true;
        App.safe_reboot();
        return;
      }
    }
  const std::string state = value(STATE);
  const bool runs = !missing(state) && brushing(state);
  float t = 0;
  const int time_value = number_of(value(TIME), &t) ? static_cast<int>(std::lround(t)) : -1;
  if (time_value != time_value_) {
    time_value_ = time_value;
    time_ms_ = now;
  }
  const int raw = sector_of(value(SECTOR));
  if (raw >= 4) decodes_six_ = true;   // a fifth or sixth sector: the integration names all six itself
  const bool fresh_sector = raw != raw_;
  if (fresh_sector) raw_ = raw;

  if (runs && !active_) {
    // A session begins: from scratch, never from the last one's peak.
    started_ms_ = now;
    sector_seen_ = false;
    highest_ = last_raw_ = corrected_ = seen_raw_ = -1;
    elapsed_ = 0;
    result_ = false;
  }
  if (runs) {
    // A sector left over from the last session counts only once it changes, or when it is the first zone.
    if (raw >= 0 && (fresh_sector || sector_seen_ || raw == 0)) {
      sector_seen_ = true;
      seen_raw_ = raw;
      // The wrap correction of sector-state.js: the oralb mapping before Home Assistant 2026.8 had no sectors 5 and 6
      // and wrapped back instead, so while brushing a reading that does not pass the highest one is the next zone.
      // The same reading again is the handle repeating itself, not progress.
      if (raw != last_raw_) {
        last_raw_ = raw;
        highest_ = raw > highest_ ? raw : std::min(highest_ + 1, 5);
        corrected_ = highest_;
      }
    }
    elapsed_ = elapsed(now);
  } else if (active_) {
    // The session ended: keep what it did.
    result_ = true;
    result_epoch_ = tessera::epoch();
    result_ms_ = now;
    result_seconds_ = elapsed_;
    result_raw_ = seen_raw_;
    result_corrected_ = corrected_;
    result_sector_seen_ = sector_seen_;
  }
  active_ = runs;
}

int Toothbrush::zone_index(int zones, int routine, int seconds, bool sector_seen, int raw, int corrected) const {
  if (sector_seen) {
    // Six zones from a brush that never named a fifth: the corrected count; otherwise the brush's own word.
    const int index = zones == 6 && !decodes_six_ ? corrected : raw;
    return std::max(0, std::min(index, zones - 1));
  }
  // No sector entity (Xiaomi, Laifen): the zones go by time, evenly over the routine.
  return std::min(zones - 1, std::max(0, seconds) * zones / std::max(1, routine));
}

Reading Toothbrush::read(const Options &o) const {
  Reading r;
  r.routine = std::max(1, o.routine);
  r.zones = !strcmp(o.zones, "6") ? 6 : !strcmp(o.zones, "4") ? 4 : decodes_six_ ? 6 : 4;

  const std::string state = value(STATE);
  float v = 0;
  if (number_of(value(BATTERY), &v)) r.battery = std::max(0, std::min(100, static_cast<int>(std::lround(v))));
  if (!missing(state)) r.charging = lower(state).find("charg") != std::string::npos;
  const std::string pressure = value(PRESSURE);
  if (!missing(pressure)) {
    const std::string p = lower(pressure);
    if (p == "on" || p.find("high") != std::string::npos) r.pressure = Pressure::TOO_HIGH;
    else if (p.find("low") != std::string::npos) r.pressure = Pressure::TOO_LOW;
    else r.pressure = Pressure::NORMAL;   // off, ok, optimal, normal
  }

  const uint32_t now = millis();
  if (active_) {
    r.phase = Phase::BRUSHING;
    r.seconds = elapsed(now);
    r.active = zone_index(r.zones, r.routine, r.seconds, sector_seen_, seen_raw_, corrected_);
    r.done = r.active;
    return r;
  }
  if (result_) {
    const uint32_t clock = tessera::epoch();
    const bool held = o.hold <= 0 ||
                      (result_epoch_ && clock ? clock - result_epoch_ < static_cast<uint32_t>(o.hold) * 3600u
                                              : since(now, result_ms_) < o.hold * 3600);
    if (held) {
      r.phase = Phase::RESULT;
      r.seconds = result_seconds_;
      r.complete = result_seconds_ >= r.routine - 5;
      // The zones before the one it stopped in, or as far as the time went, whichever is further.
      const int by_zone = result_sector_seen_
                              ? zone_index(r.zones, r.routine, result_seconds_, true, result_raw_, result_corrected_)
                              : 0;
      const int by_time = result_seconds_ * r.zones / r.routine;
      r.done = r.complete ? r.zones : std::min(r.zones, std::max(by_zone, by_time));
      return r;
    }
  }
  const std::string low = lower(state);
  r.phase = state.empty() || low == "unknown" ? Phase::WAITING : low == "unavailable" ? Phase::OFFLINE : Phase::IDLE;
  return r;
}

std::string Toothbrush::mode_text() const {
  std::string text = value(MODE);
  if (missing(text)) return "";
  // "daily_clean" as "Daily clean": the integration's own word, readable.
  std::replace(text.begin(), text.end(), '_', ' ');
  text[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(text[0])));
  return text;
}

std::string Toothbrush::head_text(const Options &o, bool *worn) const {
  *worn = false;
  const std::string head = value(HEAD);
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

std::string Toothbrush::last_text() const {
  const std::string last = value(LAST);
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
