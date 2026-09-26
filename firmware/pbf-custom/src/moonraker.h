// Link to the printer's Moonraker API (any Klipper printer; developed on a Creality K1 Max). A
// background task polls the printer state every kPollMs and sends queued G-code; nothing here
// blocks the main loop.
#pragma once

#include <Arduino.h>

namespace moonraker {

enum class PrintState : uint8_t { kUnknown, kStandby, kPrinting, kPaused, kComplete, kCancelled, kError };

struct Snapshot {
  bool fresh = false;          // last successful poll is recent (see kStaleMs)
  bool klippy_ready = false;
  PrintState print_state = PrintState::kUnknown;
  float extruder_velocity = 0;  // mm/s, negative = retracting
  float extruder_pos = 0;       // motion_report.live_position[3], resets on G92 E
  float filament_used = 0;      // print_stats.filament_used: mm the printer has extruded in this
                                // print (net of retractions, immune to G92), 0 between prints
  // The Klipper filament sensor chosen in the settings (or picked automatically) that tells
  // when the filament has reached the extruder. On a K1 that is filament_sensor_2 on the toolhead.
  bool ext_sensor = false;
  bool ext_sensor_present = false;  // a sensor is in use and the printer reports it
  float nozzle_temp = 0;        // extruder temperature / target, for the pressure test
  float nozzle_target = 0;
  bool nozzle_known = false;    // the printer reported a target (so 0 really means "off")
  bool can_extrude = false;     // Klipper allows extruding (hot enough)
  // How a G-code script tells the feeder what it is about to do. Two channels, either is
  // enough: an M117 PBF marker (needs nothing in printer.cfg) or a [gcode_macro PBF]
  // with a "mode" variable. Klipper fails a whole query that names an object it does not
  // have, so both are only asked for once the object list has shown them.
  bool pbf_macro = false;       // [gcode_macro PBF] is in the printer config
  bool pbf_display = false;     // display_status is available, so M117 markers can be read
  bool pbf_from_m117 = false;   // a recognised M117 marker is asking right now
  uint8_t pbf_mode = 0;         // 1 = free-wheel, 2 = explicitly follow K1 E motion both ways
  uint32_t age_ms = 0;          // time since the last successful poll
};

void begin();
Snapshot snapshot();
bool isPrinting();  // fresh data and print_stats.state == printing

// Queued, sent by the background task. Script is plain G-code, e.g. "PAUSE". Quiet scripts are
// only logged when the printer refuses them (for repeated small test moves).
void sendGcode(const char *script, bool quiet = false);
// True while a queued script is still waiting or running. /printer/gcode/script only answers
// once the move is done, so this also means "the printer is still executing it".
bool gcodeBusy();
// Mobileraker Companion push notification (M117 with the $MR$ prefix).
void notify(const char *title, const char *body);
// Filament sensors the printer has and the one in use: {"found":[names],"active":"name"}.
String sensorsJson();

const char *printStateName(PrintState s);
String statusText();  // one line for the web page / log

}  // namespace moonraker
