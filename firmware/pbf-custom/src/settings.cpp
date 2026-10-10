#include "settings.h"

#include <Preferences.h>
#include <errno.h>
#include <math.h>
#include <stdlib.h>

#include "config.h"
#include "logbuf.h"

namespace settings {
namespace {

Settings current;
constexpr const char *kNamespace = "pbfcfg";
constexpr size_t kNameMax = 12;    // fits the OLED lines it appears in
constexpr size_t kSensorMax = 64;
// Guards the Strings link() copies: the Moonraker task reads them on the other core while the web
// page may be replacing them here. Recursive because set() calls save().
SemaphoreHandle_t lock = nullptr;

struct Guard {
  Guard() {
    if (lock) xSemaphoreTakeRecursive(lock, portMAX_DELAY);
  }
  ~Guard() {
    if (lock) xSemaphoreGiveRecursive(lock);
  }
};

template <typename T>
T clampValue(T v, T lo, T hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

void sanitize(Settings &s) {
  s.pressure_level = clampValue<uint8_t>(s.pressure_level, hw::kMinPressureLevel, hw::kMaxPressureLevel);
  if (!isfinite(s.steps_per_mm)) s.steps_per_mm = Settings().steps_per_mm;
  s.steps_per_mm = clampValue(s.steps_per_mm, 50.0f, 1000.0f);
  s.preload_mm = clampValue<uint16_t>(s.preload_mm, 0, 300);
  s.load_mm = clampValue<uint16_t>(s.load_mm, 1, kMaxMoveMm);
  s.unload_mm = clampValue<uint16_t>(s.unload_mm, 1, kMaxMoveMm);
  s.unload_measured_mm = clampValue<uint16_t>(s.unload_measured_mm, 0, kMaxMoveMm);
  s.stuck_mm = clampValue<uint16_t>(s.stuck_mm, 30, 500);
  if (s.nofeed_mm) s.nofeed_mm = clampValue<uint16_t>(s.nofeed_mm, 30, 2000);
  s.overfeed_mm = clampValue<uint8_t>(s.overfeed_mm, 0, 25);
  s.ptest_temp = clampValue<uint16_t>(s.ptest_temp, 150, 300);
  s.load_speed = clampValue<uint8_t>(s.load_speed, 5, 60);
  s.buffer_speed = clampValue<uint8_t>(s.buffer_speed, 5, 60);
  s.unload_speed = clampValue<uint8_t>(s.unload_speed, 5, 60);
  s.brightness = clampValue<uint8_t>(s.brightness, 5, 100);
  s.brightness_ready = clampValue<uint8_t>(s.brightness_ready, 0, 100);
  s.brightness_active = clampValue<uint8_t>(s.brightness_active, 5, 100);
  s.extruder_extra_mm = clampValue<uint16_t>(s.extruder_extra_mm, 0, kMaxExtraMm);
  s.path_margin_mm = clampValue<uint8_t>(s.path_margin_mm, 0, 250);
  s.free_timeout_s = clampValue<uint16_t>(s.free_timeout_s, 5, 600);
  s.standby_min = clampValue<uint8_t>(s.standby_min, 1, 120);
  s.free_retract_mm = clampValue<uint8_t>(s.free_retract_mm, 10, 200);
  s.hold_ma = clampValue<uint16_t>(s.hold_ma, 50, hw::pressureLevelToMa(hw::kMaxPressureLevel));
  if (s.move_level) s.move_level = clampValue<uint8_t>(s.move_level, hw::kMinPressureLevel, hw::kMaxPressureLevel);
  if (s.printer_port == 0) s.printer_port = 7125;
  String name;
  for (size_t i = 0; i < s.printer_name.length(); i++) {
    char c = s.printer_name[i];
    if (c >= 0x20 && c <= 0x7e) name += c;
  }
  name.trim();
  if (name.length() > kNameMax) name.remove(kNameMax);
  name.trim();
  if (name.isEmpty()) name = Settings().printer_name;
  if (name != s.printer_name) s.printer_name = name;
  String sensor = s.sensor;
  sensor.trim();
  if (sensor.equalsIgnoreCase("auto")) sensor = "";
  if (sensor.equalsIgnoreCase("none")) sensor = "none";
  if (sensor.length() > kSensorMax) sensor.remove(kSensorMax);
  if (sensor != s.sensor) s.sensor = sensor;
  String key = s.api_key;
  key.trim();
  if (key != s.api_key) s.api_key = key;
}

}  // namespace

Settings &get() { return current; }

Link link() {
  Guard g;
  Link l;
  l.enabled = current.k1_enabled;
  l.name = current.printer_name;
  l.host = current.printer_host;
  l.port = current.printer_port;
  l.api_key = current.api_key;
  l.sensor = current.sensor;
  return l;
}

void load() {
  if (!lock) lock = xSemaphoreCreateRecursiveMutex();
  Guard g;
  Preferences prefs;
  Settings d;
  if (!prefs.begin(kNamespace, true)) {
    current = d;  // namespace does not exist yet: defaults
    return;
  }
  current.pressure_level = prefs.getUChar("pressure", d.pressure_level);
  current.buzzer = prefs.getBool("buzzer", d.buzzer);
  current.steps_per_mm = prefs.getFloat("steps_mm", d.steps_per_mm);
  current.preload_mm = prefs.getUShort("preload", d.preload_mm);
  current.load_mm = prefs.getUShort("load", d.load_mm);
  current.unload_mm = prefs.getUShort("unload", d.unload_mm);
  current.lock_move_distance = prefs.getBool("dist_lock", d.lock_move_distance);
  current.unload_measured_mm = prefs.getUShort("unload_ms", d.unload_measured_mm);
  current.stuck_mm = prefs.getUShort("stuck", d.stuck_mm);
  current.overfeed_mm = prefs.getUChar("overfeed", d.overfeed_mm);
  current.ptest_temp = prefs.getUShort("ptest_t", d.ptest_temp);
  current.idle_release = prefs.getBool("idle_rel", d.idle_release);
  current.standby = prefs.getBool("stby_on", d.standby);
  current.standby_min = prefs.getUChar("stby_min", d.standby_min);
  current.full_when_displaced = prefs.getBool("full_disp", d.full_when_displaced);
  current.hold_ma = prefs.getUShort("hold_ma", 0);
  if (!current.hold_ma) {  // upgrade from the old percentage of the run current
    uint8_t pct = prefs.getUChar("hold_pct", 0);
    current.hold_ma = pct ? (uint16_t)(hw::pressureLevelToMa(current.pressure_level) * pct / 100)
                          : d.hold_ma;
  }
  current.move_level = prefs.getUChar("move_lv", d.move_level);
  current.invert_dir = prefs.getBool("inv_dir", d.invert_dir);
  current.load_speed = prefs.getUChar("load_v", d.load_speed);
  current.buffer_speed = prefs.getUChar("buf_v", d.buffer_speed);
  current.unload_speed = prefs.getUChar("unload_v", d.unload_speed);
  current.brightness = prefs.getUChar("bri_n", d.brightness);
  current.brightness_ready = prefs.getUChar("bri_r", d.brightness_ready);
  current.brightness_active = prefs.getUChar("bri_a", d.brightness_active);
  current.k1_enabled = prefs.getBool("k1_on", d.k1_enabled);
  current.printer_name = prefs.getString("prn_name", d.printer_name);
  current.printer_host = prefs.getString("k1_host", "");
  current.sensor = prefs.getString("sensor", d.sensor);
  current.api_key = prefs.getString("api_key", "");
  current.printer_port = prefs.getUShort("k1_port", d.printer_port);
  current.lock_printing = prefs.getBool("k1_lock", d.lock_printing);
  current.pause_on_stuck = prefs.getBool("k1_pause", d.pause_on_stuck);
  current.nofeed_mm = prefs.getUShort("nofeed_mm", d.nofeed_mm);
  current.stop_at_extruder = prefs.getBool("k1_stopext", d.stop_at_extruder);
  // Was one byte ("k1_extra", max 100) before v0.10.0.
  current.extruder_extra_mm = prefs.isKey("ext_extra") ? prefs.getUShort("ext_extra", d.extruder_extra_mm)
                                                       : prefs.getUChar("k1_extra", d.extruder_extra_mm);
  current.path_margin_mm = prefs.getUChar("path_mrg", d.path_margin_mm);
  current.retract_sync = prefs.getBool("k1_rsync", d.retract_sync);
  current.free_on_unload = prefs.getBool("free_on", d.free_on_unload);
  current.free_timeout_s = prefs.getUShort("free_to", d.free_timeout_s);
  current.free_on_retract = prefs.getBool("free_ret", d.free_on_retract);
  current.free_retract_mm = prefs.getUChar("free_rmm", d.free_retract_mm);
  current.notify_runout = prefs.getBool("k1_nrun", d.notify_runout);
  current.wifi_ssid = prefs.getString("wifi_ssid", "");
  current.wifi_pass = prefs.getString("wifi_pass", "");
  prefs.end();
  sanitize(current);
}

void save() {
  Guard g;
  sanitize(current);
  Preferences prefs;
  if (!prefs.begin(kNamespace, false)) return;
  prefs.putUChar("pressure", current.pressure_level);
  prefs.putBool("buzzer", current.buzzer);
  prefs.putFloat("steps_mm", current.steps_per_mm);
  prefs.putUShort("preload", current.preload_mm);
  prefs.putUShort("load", current.load_mm);
  prefs.putUShort("unload", current.unload_mm);
  prefs.putBool("dist_lock", current.lock_move_distance);
  prefs.putUShort("unload_ms", current.unload_measured_mm);
  prefs.putUShort("stuck", current.stuck_mm);
  prefs.putUChar("overfeed", current.overfeed_mm);
  prefs.putUShort("ptest_t", current.ptest_temp);
  prefs.putBool("idle_rel", current.idle_release);
  prefs.putBool("stby_on", current.standby);
  prefs.putUChar("stby_min", current.standby_min);
  prefs.putBool("full_disp", current.full_when_displaced);
  prefs.putUShort("hold_ma", current.hold_ma);
  prefs.putUChar("move_lv", current.move_level);
  prefs.putBool("inv_dir", current.invert_dir);
  prefs.putUChar("load_v", current.load_speed);
  prefs.putUChar("buf_v", current.buffer_speed);
  prefs.putUChar("unload_v", current.unload_speed);
  prefs.putUChar("bri_n", current.brightness);
  prefs.putUChar("bri_r", current.brightness_ready);
  prefs.putUChar("bri_a", current.brightness_active);
  prefs.putBool("k1_on", current.k1_enabled);
  prefs.putString("prn_name", current.printer_name);
  prefs.putString("k1_host", current.printer_host);
  prefs.putString("sensor", current.sensor);
  prefs.putString("api_key", current.api_key);
  prefs.putUShort("k1_port", current.printer_port);
  prefs.putBool("k1_lock", current.lock_printing);
  prefs.putBool("k1_pause", current.pause_on_stuck);
  prefs.putUShort("nofeed_mm", current.nofeed_mm);
  prefs.putBool("k1_stopext", current.stop_at_extruder);
  prefs.putUShort("ext_extra", current.extruder_extra_mm);
  if (prefs.isKey("k1_extra")) prefs.remove("k1_extra");
  prefs.putUChar("path_mrg", current.path_margin_mm);
  prefs.putBool("k1_rsync", current.retract_sync);
  prefs.putBool("free_on", current.free_on_unload);
  prefs.putUShort("free_to", current.free_timeout_s);
  prefs.putBool("free_ret", current.free_on_retract);
  prefs.putUChar("free_rmm", current.free_retract_mm);
  prefs.putBool("k1_nrun", current.notify_runout);
  prefs.putString("wifi_ssid", current.wifi_ssid);
  prefs.putString("wifi_pass", current.wifi_pass);
  prefs.end();
}

void setWifi(const String &ssid, const String &pass) {
  current.wifi_ssid = ssid;
  current.wifi_pass = pass;
  save();
}

void appendJson(String &out) {
  const Settings &s = current;
  out += "{\"pressure\":" + String(s.pressure_level);
  out += ",\"pressure_ma\":" + String(hw::pressureLevelToMa(s.pressure_level));
  out += ",\"buzzer\":" + String(s.buzzer ? "true" : "false");
  out += ",\"steps_mm\":" + String(s.steps_per_mm, 2);
  out += ",\"preload_mm\":" + String(s.preload_mm);
  out += ",\"load_mm\":" + String(s.load_mm);
  out += ",\"unload_mm\":" + String(s.unload_mm);
  out += ",\"lock_move_distance\":" + String(s.lock_move_distance ? "true" : "false");
  out += ",\"unload_measured_mm\":" + String(s.unload_measured_mm);
  out += ",\"stuck_mm\":" + String(s.stuck_mm);
  out += ",\"overfeed_mm\":" + String(s.overfeed_mm);
  out += ",\"ptest_temp\":" + String(s.ptest_temp);
  out += ",\"idle_release\":" + String(s.idle_release ? "true" : "false");
  out += ",\"standby\":" + String(s.standby ? "true" : "false");
  out += ",\"standby_min\":" + String(s.standby_min);
  out += ",\"full_when_displaced\":" + String(s.full_when_displaced ? "true" : "false");
  out += ",\"hold_ma\":" + String(s.hold_ma);
  out += ",\"move_level\":" + String(s.move_level);
  out += ",\"move_ma\":" + String(hw::pressureLevelToMa(s.move_level ? s.move_level : s.pressure_level));
  out += ",\"invert_dir\":" + String(s.invert_dir ? "true" : "false");
  out += ",\"load_speed\":" + String(s.load_speed);
  out += ",\"buffer_speed\":" + String(s.buffer_speed);
  out += ",\"unload_speed\":" + String(s.unload_speed);
  out += ",\"brightness\":" + String(s.brightness);
  out += ",\"brightness_ready\":" + String(s.brightness_ready);
  out += ",\"brightness_active\":" + String(s.brightness_active);
  out += ",\"k1_enabled\":" + String(s.k1_enabled ? "true" : "false");
  out += ",\"printer_name\":";
  logbuf::appendJsonString(out, s.printer_name.c_str());
  out += ",\"sensor\":";
  logbuf::appendJsonString(out, s.sensor.c_str());
  out += ",\"printer_host\":";
  logbuf::appendJsonString(out, s.printer_host.c_str());
  out += ",\"printer_port\":" + String(s.printer_port);
  out += ",\"api_key_set\":" + String(s.api_key.length() ? "true" : "false");
  out += ",\"lock_printing\":" + String(s.lock_printing ? "true" : "false");
  out += ",\"pause_on_stuck\":" + String(s.pause_on_stuck ? "true" : "false");
  out += ",\"nofeed_mm\":" + String(s.nofeed_mm);
  out += ",\"stop_at_extruder\":" + String(s.stop_at_extruder ? "true" : "false");
  out += ",\"extruder_extra_mm\":" + String(s.extruder_extra_mm);
  out += ",\"path_margin_mm\":" + String(s.path_margin_mm);
  out += ",\"retract_sync\":" + String(s.retract_sync ? "true" : "false");
  out += ",\"free_on_unload\":" + String(s.free_on_unload ? "true" : "false");
  out += ",\"free_timeout_s\":" + String(s.free_timeout_s);
  out += ",\"free_on_retract\":" + String(s.free_on_retract ? "true" : "false");
  out += ",\"free_retract_mm\":" + String(s.free_retract_mm);
  out += ",\"notify_runout\":" + String(s.notify_runout ? "true" : "false");
  out += ",\"wifi_ssid\":";
  logbuf::appendJsonString(out, s.wifi_ssid.c_str());
  out += '}';
}

void resetToDefaults() {
  Guard g;
  Settings fresh;
  fresh.wifi_ssid = current.wifi_ssid;  // keep the network
  fresh.wifi_pass = current.wifi_pass;
  fresh.printer_host = current.printer_host;
  fresh.printer_port = current.printer_port;
  fresh.printer_name = current.printer_name;
  fresh.sensor = current.sensor;
  fresh.api_key = current.api_key;
  current = fresh;
  save();
}

namespace {

// Parse and check before narrowing to uint8_t/uint16_t. Invalid input leaves the
// old setting intact, including when a web request batches changes without saving.
template <typename T>
bool assignInteger(T &field, const String &value, long lo, long hi, bool allow_zero = false) {
  String input = value;
  input.trim();
  char *end;
  errno = 0;
  long n = strtol(input.c_str(), &end, 10);
  if (input.isEmpty() || *end || errno == ERANGE ||
      (!(allow_zero && n == 0) && (n < lo || n > hi))) return false;
  field = static_cast<T>(n);
  return true;
}

bool assignSteps(float &field, const String &value) {
  String input = value;
  input.trim();
  char *end;
  errno = 0;
  float n = strtof(input.c_str(), &end);
  if (input.isEmpty() || *end || errno == ERANGE || !isfinite(n) || n < 50 || n > 1000) return false;
  field = n;
  return true;
}

bool applyValue(Settings &s, const String &name, const String &value) {
  bool valid = true;
  bool on = value == "1" || value.equalsIgnoreCase("on") || value.equalsIgnoreCase("true");
  if (name == "pressure") valid = assignInteger(s.pressure_level, value, 1, 10);
  else if (name == "buzzer") s.buzzer = on;
  else if (name == "steps_mm") valid = assignSteps(s.steps_per_mm, value);
  else if (name == "preload_mm") valid = assignInteger(s.preload_mm, value, 0, 300);
  else if (name == "load_mm") valid = assignInteger(s.load_mm, value, 1, kMaxMoveMm);
  else if (name == "unload_mm") valid = assignInteger(s.unload_mm, value, 1, kMaxMoveMm);
  else if (name == "lock_move_distance") {
    s.lock_move_distance = on;
    if (on) s.unload_measured_mm = 0;  // discard a possibly bad learned limit from a jam
  }
  else if (name == "unload_measured_mm") valid = assignInteger(s.unload_measured_mm, value, 0, kMaxMoveMm);
  else if (name == "stuck_mm") valid = assignInteger(s.stuck_mm, value, 30, 500);
  else if (name == "overfeed_mm") valid = assignInteger(s.overfeed_mm, value, 0, 25);
  else if (name == "ptest_temp") valid = assignInteger(s.ptest_temp, value, 150, 300);
  else if (name == "idle_release") s.idle_release = on;
  else if (name == "standby") s.standby = on;
  else if (name == "standby_min") valid = assignInteger(s.standby_min, value, 1, 120);
  else if (name == "full_when_displaced") s.full_when_displaced = on;
  else if (name == "hold_ma") valid = assignInteger(s.hold_ma, value, 50, 700);
  else if (name == "move_level") valid = assignInteger(s.move_level, value, 0, 10);
  else if (name == "invert_dir") s.invert_dir = on;
  else if (name == "load_speed") valid = assignInteger(s.load_speed, value, 5, 60);
  else if (name == "buffer_speed") valid = assignInteger(s.buffer_speed, value, 5, 60);
  else if (name == "unload_speed") valid = assignInteger(s.unload_speed, value, 5, 60);
  else if (name == "brightness") valid = assignInteger(s.brightness, value, 5, 100);
  else if (name == "brightness_ready") valid = assignInteger(s.brightness_ready, value, 0, 100);
  else if (name == "brightness_active") valid = assignInteger(s.brightness_active, value, 5, 100);
  else if (name == "k1_enabled") s.k1_enabled = on;
  else if (name == "printer_name") s.printer_name = value;
  else if (name == "printer_host") s.printer_host = value;
  else if (name == "sensor") s.sensor = value;
  else if (name == "api_key") s.api_key = value;
  else if (name == "printer_port") valid = assignInteger(s.printer_port, value, 1, 65535);
  else if (name == "lock_printing") s.lock_printing = on;
  else if (name == "pause_on_stuck") s.pause_on_stuck = on;
  else if (name == "nofeed_mm") valid = assignInteger(s.nofeed_mm, value, 30, 2000, true);
  else if (name == "stop_at_extruder") s.stop_at_extruder = on;
  else if (name == "extruder_extra_mm") valid = assignInteger(s.extruder_extra_mm, value, 0, kMaxExtraMm);
  else if (name == "path_margin_mm") valid = assignInteger(s.path_margin_mm, value, 0, 250);
  else if (name == "retract_sync") s.retract_sync = on;
  else if (name == "free_on_unload") s.free_on_unload = on;
  else if (name == "free_timeout_s") valid = assignInteger(s.free_timeout_s, value, 5, 600);
  else if (name == "free_on_retract") s.free_on_retract = on;
  else if (name == "free_retract_mm") valid = assignInteger(s.free_retract_mm, value, 10, 200);
  else if (name == "notify_runout") s.notify_runout = on;
  else return false;
  return valid;
}

}  // namespace

bool validate(const String &name, const String &value) {
  Guard g;
  Settings candidate = current;
  return applyValue(candidate, name, value);
}

bool set(const String &name, const String &value, bool persist) {
  Guard g;
  if (!applyValue(current, name, value)) return false;
  if (persist) save();
  return true;
}

void print(Print &out) {
  const Settings &s = current;
  out.printf("pressure=%u (%u mA) buzzer=%d steps_mm=%.2f\n", s.pressure_level,
             hw::pressureLevelToMa(s.pressure_level), s.buzzer, s.steps_per_mm);
  out.printf("preload_mm=%u load_mm=%u unload_mm=%u distance_lock=%d stuck_mm=%u idle_release=%d "
             "invert_dir=%d\n",
             s.preload_mm, s.load_mm, s.unload_mm, s.lock_move_distance, s.stuck_mm,
             s.idle_release, s.invert_dir);
  out.printf("load_speed=%u buffer_speed=%u unload_speed=%u (mm/s)\n", s.load_speed, s.buffer_speed,
             s.unload_speed);
  out.printf("standby=%d standby_min=%u\n", s.standby, s.standby_min);
  out.printf("printer_name=%s printer=%s:%u sensor=%s\n", s.printer_name.c_str(),
             s.printer_host.c_str(), s.printer_port, s.sensor.length() ? s.sensor.c_str() : "auto");
}

}  // namespace settings
