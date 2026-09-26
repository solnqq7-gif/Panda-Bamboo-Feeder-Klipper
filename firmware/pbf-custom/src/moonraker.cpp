#include "moonraker.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>

#include <vector>

#include "logbuf.h"
#include "settings.h"

namespace moonraker {
namespace {

constexpr uint32_t kPollMs = 200;
constexpr uint32_t kStaleMs = 3000;
constexpr uint32_t kHttpTimeoutMs = 1500;
constexpr uint32_t kGcodeTimeoutMs = 15000;  // PAUSE waits for the moves to finish
constexpr size_t kGcodeLen = 200;
constexpr const char *kQuery =
    "/printer/objects/query?print_stats=state,filament_used&webhooks=state"
    "&motion_report=live_extruder_velocity,live_position"
    "&extruder=temperature,target,can_extrude";
// Only appended once the objects are known to exist. Current Klipper answers an unknown object
// with nulls, but that is not promised anywhere, and asking for nothing is cheaper anyway.
constexpr const char *kPbfQuery = "&gcode_macro%20PBF=mode";
constexpr const char *kDisplayQuery = "&display_status=message";
constexpr const char *kObjectList = "/printer/objects/list";
constexpr uint32_t kProbeMs = 60000;  // re-check this often while something is missing
constexpr const char *kMarker = "PBF:";
// Printers name their sensors freely, so the list of what exists comes from the printer, and the
// setting (or a guess, see pickSensor) picks one. Only switch sensors: a filament_motion_sensor
// reports "not detected" from boot until the filament moves, which would read as an empty
// extruder and stop the buffer from topping up.
constexpr const char *kSensorTypes[] = {"filament_switch_sensor "};

struct GcodeItem {
  char script[kGcodeLen];
  bool quiet;
};

SemaphoreHandle_t lock = nullptr;
QueueHandle_t gcode_queue = nullptr;
Snapshot shared;              // guarded by lock
uint32_t last_ok_ms = 0;      // guarded by lock
bool was_fresh = false;       // task only
bool macro_present = false;   // task only: [gcode_macro PBF] is in the printer config
bool display_present = false; // task only: display_status is queryable (M117 marker channel)
bool probed = false;
uint32_t probe_ms = 0;
String probed_sensor_setting; // task only: the sensor setting the last probe resolved
String sensor_obj;            // task only: the sensor being polled, "" = none
String extra_query;           // task only: the parts of the query that are safe to ask for
String sensors_json = "{\"found\":[],\"active\":\"\"}";  // guarded by lock
Link link_cfg;                // task only: copied from the settings once per loop
uint8_t poll_fails = 0;
volatile int gcode_pending = 0;  // queued + in flight

PrintState parseState(const char *s) {
  if (!s) return PrintState::kUnknown;
  if (!strcmp(s, "standby")) return PrintState::kStandby;
  if (!strcmp(s, "printing")) return PrintState::kPrinting;
  if (!strcmp(s, "paused")) return PrintState::kPaused;
  if (!strcmp(s, "complete")) return PrintState::kComplete;
  if (!strcmp(s, "cancelled")) return PrintState::kCancelled;
  if (!strcmp(s, "error")) return PrintState::kError;
  return PrintState::kUnknown;
}

const char *pname() { return link_cfg.name.c_str(); }

String baseUrl() { return "http://" + link_cfg.host + ":" + String(link_cfg.port); }

// Every request goes through here so the API key rides along. HTTPClient drops added headers at
// end(), so it has to be added each time.
bool openRequest(HTTPClient &http, const String &path, uint32_t timeout_ms) {
  http.setTimeout(timeout_ms);
  if (!http.begin(baseUrl() + path)) return false;
  if (link_cfg.api_key.length()) http.addHeader("X-Api-Key", link_cfg.api_key);
  return true;
}

// Moonraker answers 401/403 when it neither trusts the feeder's address nor gets a valid key.
// Say once what to do about it; a working poll clears it again.
volatile int auth_code = 0;

void noteHttpCode(int code) {
  if (code != 401 && code != 403) return;
  if (auth_code != code) {
    logbuf::logf("%s: Moonraker refused the feeder (HTTP %d): %s", pname(), code,
                 link_cfg.api_key.length()
                     ? "the API key in the settings is wrong"
                     : "set its API key in the settings, or add the feeder's IP to "
                       "[authorization] trusted_clients in moonraker.conf");
  }
  auth_code = code;
}

String urlEncode(const String &v) {
  String out;
  for (size_t i = 0; i < v.length(); i++) {
    char c = v[i];
    if (isalnum((unsigned char)c) || c == '_' || c == '-' || c == '.') {
      out += c;
    } else {
      char hex[4];
      snprintf(hex, sizeof(hex), "%%%02X", (uint8_t)c);
      out += hex;
    }
  }
  return out;
}

// Every filament sensor in the printer's object list, in config order.
std::vector<String> findSensors(const String &body) {
  std::vector<String> found;
  int at = 0;
  while ((at = body.indexOf("\"filament_", at)) >= 0) {
    at++;  // past the quote
    for (const char *type : kSensorTypes) {
      if (!body.startsWith(type, at)) continue;
      int end = body.indexOf('"', at);
      if (end > at) found.push_back(body.substring(at, end));
      break;
    }
  }
  return found;
}

String shortName(const String &obj) {
  int sp = obj.indexOf(' ');
  return sp < 0 ? obj : obj.substring(sp + 1);
}

// The setting names a sensor (with or without its type), says "none", or is empty: then guess.
// A sensor at the toolhead is the one that knows the filament reached the extruder, and
// toolhead boards are usually configured after the frame, so the last one listed wins.
String pickSensor(const std::vector<String> &found, const String &setting) {
  if (setting == "none" || found.empty()) return "";
  if (setting.length()) {
    for (const String &obj : found) {
      if (obj.equalsIgnoreCase(setting) || shortName(obj).equalsIgnoreCase(setting)) return obj;
    }
    return "";
  }
  for (const char *hint : {"toolhead", "extruder", "nozzle", "head", "tool"}) {
    for (const String &obj : found) {
      String n = shortName(obj);
      n.toLowerCase();
      if (n.indexOf(hint) >= 0) return obj;
    }
  }
  return found.back();
}

// The printer config can change at any Klipper restart, so this is a plain substring check on
// the object list rather than something cached forever.
void probeObjects(HTTPClient &http) {
  if (!openRequest(http, kObjectList, kHttpTimeoutMs)) return;
  int code = http.GET();
  noteHttpCode(code);
  bool ok = code == 200;
  String body = ok ? http.getString() : String();
  http.end();
  if (!ok) return;  // printer not answering yet: try again on the next loop
  probed = true;
  probe_ms = millis();
  bool macro = body.indexOf("\"gcode_macro PBF\"") >= 0;
  bool disp = body.indexOf("\"display_status\"") >= 0;
  std::vector<String> found = findSensors(body);
  String sensor = pickSensor(found, link_cfg.sensor);
  static bool sensor_logged = false;
  bool sensor_news = !sensor_logged || sensor != sensor_obj || link_cfg.sensor != probed_sensor_setting;
  sensor_logged = true;
  if (macro != macro_present || disp != display_present) {
    logbuf::logf("%s: unload signal: M117 marker %s, [gcode_macro PBF] %s", pname(),
                 disp ? "on" : "unavailable", macro ? "found" : "not in the printer config");
  }
  probed_sensor_setting = link_cfg.sensor;
  macro_present = macro;
  display_present = disp;
  sensor_obj = sensor;
  extra_query = String(macro ? kPbfQuery : "") + (disp ? kDisplayQuery : "");
  if (sensor.length()) extra_query += "&" + urlEncode(sensor) + "=filament_detected";
  if (sensor_news) {
    String names;
    for (const String &obj : found) names += (names.length() ? ", " : "") + shortName(obj);
    if (sensor.length()) {
      logbuf::logf("%s: filament sensor \"%s\" (%s; found: %s)", pname(), shortName(sensor).c_str(),
                   link_cfg.sensor.length() ? "chosen in the settings" : "picked automatically",
                   names.c_str());
    } else if (link_cfg.sensor == "none") {
      logbuf::logf("%s: filament sensor not used (turned off in the settings)", pname());
    } else if (link_cfg.sensor.length()) {
      logbuf::logf("%s: filament sensor \"%s\" not found (found: %s)", pname(),
                   link_cfg.sensor.c_str(), names.length() ? names.c_str() : "none");
    } else {
      logbuf::logf("%s: no filament sensor found, loads stop by distance only", pname());
    }
  }
  String json = "{\"found\":[";
  for (size_t i = 0; i < found.size(); i++) {
    if (i) json += ',';
    logbuf::appendJsonString(json, shortName(found[i]).c_str());
  }
  json += "],\"active\":";
  logbuf::appendJsonString(json, shortName(sensor).c_str());
  json += '}';
  xSemaphoreTake(lock, portMAX_DELAY);
  sensors_json = json;
  xSemaphoreGive(lock);
}

// M117 PBF:FREE releases the gear; M117 PBF:FOLLOW asks the feeder to mirror positive and
// negative K1 E motion. Tokens are case-insensitive because hand-written G-code commonly uses
// lower case.
// Read with strstr so a prefix such as Mobileraker's own "$MR$:" does not hide the marker.
bool startsWithNoCase(const char *value, const char *token) {
  while (*token) {
    char a = *value++;
    char b = *token++;
    if (a >= 'a' && a <= 'z') a -= 'a' - 'A';
    if (b >= 'a' && b <= 'z') b -= 'a' - 'A';
    if (a != b) return false;
  }
  return true;
}

const char *findNoCase(const char *value, const char *token) {
  for (; *value; ++value) {
    if (startsWithNoCase(value, token)) return value;
  }
  return nullptr;
}

uint8_t markerMode(const char *msg) {
  if (!msg) return 0;
  const char *p = findNoCase(msg, kMarker);
  if (!p) return 0;
  p += strlen(kMarker);
  if (startsWithNoCase(p, "FREE")) return 1;
  if (startsWithNoCase(p, "FOLLOW")) return 2;
  return 0;
}

bool poll(HTTPClient &http) {
  if (!openRequest(http, String(kQuery) + extra_query, kHttpTimeoutMs)) return false;
  int code = http.GET();
  noteHttpCode(code);
  if (code != 200) {
    http.end();
    return false;
  }
  auth_code = 0;
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, http.getString());
  http.end();
  if (err) return false;
  JsonObject st = doc["result"]["status"];
  Snapshot snap;
  snap.fresh = true;
  snap.klippy_ready = strcmp(st["webhooks"]["state"] | "", "ready") == 0;
  snap.print_state = parseState(st["print_stats"]["state"] | "");
  snap.filament_used = st["print_stats"]["filament_used"] | 0.0f;
  snap.extruder_velocity = st["motion_report"]["live_extruder_velocity"] | 0.0f;
  snap.extruder_pos = st["motion_report"]["live_position"][3] | 0.0f;
  if (sensor_obj.length()) {
    JsonVariant sensor = st[sensor_obj]["filament_detected"];
    snap.ext_sensor_present = !sensor.isNull();
    snap.ext_sensor = sensor | false;
  }
  snap.nozzle_temp = st["extruder"]["temperature"] | 0.0f;
  snap.nozzle_target = st["extruder"]["target"] | 0.0f;
  snap.nozzle_known = !st["extruder"]["target"].isNull();
  snap.can_extrude = st["extruder"]["can_extrude"] | false;
  snap.pbf_macro = macro_present;
  snap.pbf_display = display_present;
  // Klipper stores the variable as whatever literal the config or SET_GCODE_VARIABLE gave it,
  // so read it as a number and round: "1" and "1.0" both mean the same thing.
  float mode = st["gcode_macro PBF"]["mode"] | 0.0f;
  uint8_t macro_mode = (uint8_t)constrain((int)lroundf(mode), 0, 255);
  uint8_t marker_mode = markerMode(st["display_status"]["message"] | "");
  snap.pbf_from_m117 = marker_mode != 0;
  // Either channel is enough, so whichever one the G-code uses works without a setting.
  snap.pbf_mode = marker_mode ? marker_mode : macro_mode;
  xSemaphoreTake(lock, portMAX_DELAY);
  PrintState before = shared.print_state;
  shared = snap;
  last_ok_ms = millis();
  xSemaphoreGive(lock);
  if (before != snap.print_state) {
    logbuf::logf("%s: print state %s -> %s", pname(), printStateName(before),
                 printStateName(snap.print_state));
  }
  return true;
}

void postGcode(HTTPClient &http, const char *script, bool quiet) {
  if (!openRequest(http, "/printer/gcode/script", kGcodeTimeoutMs)) return;
  http.addHeader("Content-Type", "application/json");
  JsonDocument body;
  body["script"] = script;
  String payload;
  serializeJson(body, payload);
  int code = http.POST(payload);
  http.end();
  if (!quiet || code != 200) logbuf::logf("%s: sent \"%s\" -> HTTP %d", pname(), script, code);
}

void task(void *) {
  HTTPClient http;
  http.setReuse(true);
  for (;;) {
    link_cfg = settings::link();
    if (!link_cfg.enabled || link_cfg.host.isEmpty() || WiFi.status() != WL_CONNECTED) {
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }
    GcodeItem item;
    while (xQueueReceive(gcode_queue, &item, 0) == pdTRUE) {
      postGcode(http, item.script, item.quiet);
      if (gcode_pending > 0) gcode_pending--;
    }

    bool missing = !(macro_present && display_present) ||
                   (sensor_obj.isEmpty() && link_cfg.sensor != "none");
    if (!probed || link_cfg.sensor != probed_sensor_setting ||
        (missing && millis() - probe_ms > kProbeMs)) {
      probeObjects(http);
    }
    bool ok = poll(http);
    // A query naming a macro the printer no longer has fails outright, which would take the
    // whole link down. Two failures in a row: drop back to the plain query and look again.
    if (ok) {
      poll_fails = 0;
    } else if (!extra_query.isEmpty() && ++poll_fails >= 2) {
      logbuf::logf("%s: query failed twice, asking the printer what it has again", pname());
      macro_present = display_present = probed = false;
      sensor_obj = "";
      extra_query = "";
      poll_fails = 0;
    }
    bool fresh = snapshot().fresh;
    if (fresh != was_fresh) {
      logbuf::logf("%s: %s (%s)", pname(), fresh ? "connected" : "connection lost",
                   baseUrl().c_str());
      was_fresh = fresh;
      if (fresh) probed = false;  // Klipper may have restarted with a different config
    }
    vTaskDelay(pdMS_TO_TICKS(ok ? kPollMs : 1000));
  }
}

}  // namespace

void begin() {
  lock = xSemaphoreCreateMutex();
  gcode_queue = xQueueCreate(4, sizeof(GcodeItem));
  xTaskCreatePinnedToCore(task, "moonraker", 8192, nullptr, 1, nullptr, 0);
}

Snapshot snapshot() {
  xSemaphoreTake(lock, portMAX_DELAY);
  Snapshot s = shared;
  s.age_ms = last_ok_ms ? millis() - last_ok_ms : UINT32_MAX;
  s.fresh = last_ok_ms && s.age_ms < kStaleMs;
  xSemaphoreGive(lock);
  return s;
}

bool isPrinting() {
  Snapshot s = snapshot();
  return s.fresh && s.print_state == PrintState::kPrinting;
}

void sendGcode(const char *script, bool quiet) {
  if (!gcode_queue || !settings::get().k1_enabled) return;
  GcodeItem item;
  strncpy(item.script, script, kGcodeLen - 1);
  item.script[kGcodeLen - 1] = '\0';
  item.quiet = quiet;
  gcode_pending++;
  if (xQueueSend(gcode_queue, &item, 0) != pdTRUE) {
    gcode_pending--;
    logbuf::line("printer: G-code queue full, dropped");
  }
}

bool gcodeBusy() { return gcode_pending > 0; }

String sensorsJson() {
  xSemaphoreTake(lock, portMAX_DELAY);
  String s = sensors_json;
  xSemaphoreGive(lock);
  return s;
}

void notify(const char *title, const char *body) {
  char script[kGcodeLen];
  snprintf(script, sizeof(script), "M117 $MR$:%s|%s", title, body);
  sendGcode(script);
}

const char *printStateName(PrintState s) {
  switch (s) {
    case PrintState::kStandby: return "standby";
    case PrintState::kPrinting: return "printing";
    case PrintState::kPaused: return "paused";
    case PrintState::kComplete: return "complete";
    case PrintState::kCancelled: return "cancelled";
    case PrintState::kError: return "error";
    default: return "unknown";
  }
}

String statusText() {
  const Settings &cfg = settings::get();
  if (!cfg.k1_enabled) return "off";
  Snapshot s = snapshot();
  if (!s.fresh) return auth_code ? "unauthorized" : "not connected";
  return String(printStateName(s.print_state)) + (s.klippy_ready ? "" : " (klippy not ready)");
}

}  // namespace moonraker
