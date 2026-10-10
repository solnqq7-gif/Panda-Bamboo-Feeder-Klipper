// User settings, stored in NVS (Preferences namespace "pbfcfg").
#pragma once

#include <Arduino.h>

struct Settings {
  uint8_t pressure_level = 7;   // 1..10, see hw::pressureLevelToMa()
  bool buzzer = true;
  float steps_per_mm = 250.0f;  // measured ~261, stock constant suggests 248
  uint16_t preload_mm = 100;    // fed after the filament reaches the drive gear
  uint16_t load_mm = 500;       // feeder outlet -> printer extruder path length (auto-calibrated)
  uint16_t unload_mm = 600;     // distance for the web "retract by distance" button
  bool lock_move_distance = false;  // keep the configured load/unload distances fixed instead
                                    // of replacing them with automatically learned values
  uint16_t unload_measured_mm = 0;  // how far a full unload actually needed, learned from the last
                                    // successful one. 0 = never measured, fall back to the
                                    // preload + load estimate.
  uint16_t stuck_mm = 80;       // feeding this far without the carriage returning = stuck
  uint8_t overfeed_mm = 8;      // fed after the carriage leaves its rest position: how much slack
                                // the buffer parks with. More = fewer top-ups; the buffer test
                                // sets it to the measured travel minus room for retractions.
  bool idle_release = false;    // driver off when idle. The buffer only works with this OFF (the
                                // gear has to hold the filament); the full test switches it off.
  bool standby = true;          // READY lets go of the filament (no standstill current, no heat)
                                // once the K1 has been idle for standby_min: not printing, not
                                // heating and too cold to extrude. Heating wakes it up again.
  uint8_t standby_min = 5;
  bool full_when_displaced = true;  // carriage pushed out = buffer full (stock behaviour)
  uint16_t hold_ma = 300;      // standstill current in mA (capped at the run current). This is
                               // what the gear holds the filament back with all print long, so it
                               // is the one that heats the driver; the pressure test measures it.
  uint8_t move_level = 8;      // run current for loads, unloads and manual moves: they drag the
                               // filament through the whole path, which needs far more torque
                               // than a short buffer top-up. 0 = use pressure_level.
  bool invert_dir = false;      // DIR HIGH = feed when false
  uint8_t load_speed = 30;      // mm/s
  uint8_t buffer_speed = 20;    // mm/s
  uint8_t unload_speed = 30;    // mm/s
  uint8_t brightness = 80;      // OLED % in most states
  uint8_t brightness_ready = 10;   // OLED % while READY and idle
  uint8_t brightness_active = 80;  // OLED % while buffer-feeding from READY (+5 s)
  // Printer (Moonraker) link, host/port imported from the stock firmware settings once. Works
  // with any Klipper printer; the k1_ prefix on some names is historical (first used on a K1).
  bool k1_enabled = true;
  String printer_name = "Printer";  // shown on the web page, OLED, log and push notifications.
                                    // Printable ASCII only, the OLED font has nothing else.
  String printer_host;
  uint16_t printer_port = 7125;
  // Moonraker API key, sent as X-Api-Key. Only needed when Moonraker does not trust the feeder's
  // address ([authorization] trusted_clients). Never sent back to the web page.
  String api_key;
  // Klipper filament sensor that tells when the filament has reached the extruder, as an object
  // name ("filament_switch_sensor x") or just the sensor name. Switch sensors only.
  // Empty = pick one from the printer's object list, "none" = do not use a sensor.
  String sensor;
  bool lock_printing = true;     // keys and web moves disabled while the printer is printing
  bool pause_on_stuck = true;    // send PAUSE + push notification on a stuck alarm
  uint16_t nofeed_mm = 100;      // printing, and the printer has extruded this much without the
                                 // buffer ever emptying: the filament is not coming through the
                                 // feeder, alarm like a jam. 0 = off.
  bool stop_at_extruder = true;  // a load ends when the filament sensor sees the filament
  uint16_t extruder_extra_mm = 10;  // fed after the sensor triggers; up to kMaxExtraMm for a
                                    // sensor far from the extruder (next to the spool)
  uint8_t path_margin_mm = 20;     // added to the measured gear -> toolhead distance (see 7.3)
  uint16_t ptest_temp = 230;       // nozzle temperature the pressure test heats to first
  bool retract_sync = true;      // follow K1 extruder retractions while not printing
  bool free_on_unload = true;    // let go of the filament while [gcode_macro PBF] mode is 1, so
                                 // the printer's tip-forming moves do not fight the gear
  uint16_t free_timeout_s = 60;  // grip again after this long no matter what the printer says
  bool free_on_retract = true;   // fallback for unloads that carry no marker (the printer's own
                                 // screen, another UI): a retraction this long while not
                                 // printing is an unload, so let go rather than chase it
  uint8_t free_retract_mm = 25;  // a print retraction is a few mm; PAUSE on this printer is 3
  bool notify_runout = true;     // push notification when the tail leaves the feeder
  String wifi_ssid;             // empty = not configured (imported from the stock firmware once)
  String wifi_pass;
};

// What the Moonraker task needs. It runs on the other core, so it takes a copy (see link())
// instead of reading Strings the web page may be replacing at that moment.
struct Link {
  bool enabled = false;
  String name;
  String host;
  uint16_t port = 0;
  String api_key;
  String sensor;
};

namespace settings {
constexpr uint16_t kMaxExtraMm = 5000;   // extruder_extra_mm
constexpr uint16_t kMaxMoveMm = 10000;   // load / unload distances, long enough for the extra above

Settings &get();
Link link();  // safe to call from any task
void load();
void save();
void resetToDefaults();
// Check a proposed value without changing settings (used before applying a web batch).
bool validate(const String &name, const String &value);
// Sets a value by name, e.g. set("load_mm", "650"). False = unknown name or invalid number;
// invalid input leaves the existing setting unchanged.
bool set(const String &name, const String &value, bool persist = true);
void print(Print &out);
void setWifi(const String &ssid, const String &pass);
// Settings as a JSON object (without the WiFi password).
void appendJson(String &out);
}  // namespace settings
