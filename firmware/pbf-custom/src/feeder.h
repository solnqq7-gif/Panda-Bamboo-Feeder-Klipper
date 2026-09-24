// Feeder state machine: insert, preload, load, buffer feeding, runout hand-off, unload,
// stuck detection, the pressure / buffer / full tests and the path length calibration.
#pragma once

#include <Arduino.h>

namespace feeder {

void begin();
void update();  // call from loop()

// Console hooks (same as the keys, but usable over serial)
void cmdLoad();
void cmdUnload();
void cmdStop();
void cmdJog(float mm);
void cmdFeedDistance(float mm);    // like a load, for an arbitrary distance
void cmdUnloadDistance(float mm);  // retract at most mm (stops early when the tip leaves the gear)
void cmdHold(bool feed);           // web "hold" button: call repeatedly while pressed
void cmdHoldStop();
// Release the drive gear so the printer can pull the filament itself (tip forming / unload),
// or grip it again. Normally driven by [gcode_macro PBF] variable "mode" on the printer.
void cmdFree(bool on);
// Sweeps the pressure levels: the printer consumes a fixed amount at each one and the feeder
// has to keep the buffer full. Calling it again while it runs stops it.
void cmdPressureTest();
// Pushes the carriage to the end of its travel to measure how much the buffer can store, and
// sets the parking amount (overfeed_mm) with room left for retractions. No filament, no heat.
void cmdBufferTest();
// First install: the buffer test, then the pressure test with the new parking amount.
void cmdFullTest();
// Any of the three again while one runs stops it.
void cmdNotifyTest();
void setVerbose(bool on);
void wakeScreen();  // a web action: full brightness for a while, and ends standby
void printStatus(Print &out);
// Feeder state as JSON object members (no braces), for the web API.
void appendStatusJson(String &out);

}  // namespace feeder
