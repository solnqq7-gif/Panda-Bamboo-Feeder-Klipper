// Lifetime counters kept in flash (NVS namespace "pbfstat").
//
// Deliberately small and defensive: the block is a fixed size that never grows, it is written at
// most once every kFlushMs (plus once when a print ends), and every read checks a magic, a layout
// version, the firmware version and a checksum. Anything unexpected wipes the block and starts
// from zero rather than trusting it. The feeder runs normally whether or not any of this works.
#pragma once

#include <Arduino.h>

namespace stats {

void begin();
void tick();   // call from loop(); flushes at most once every kFlushMs
void flush();  // write now if anything changed (used when a print finishes)
void clear();  // wipe the counters and the stored block

void addMoved(float mm);      // signed: + towards the printer, - back towards the box
void addTopUps(uint16_t n, float mm);
void addStuck();
void addUnloadJam();
void setUartHiccups(uint32_t total_since_boot);

void appendJson(String &out);  // as a JSON object

}  // namespace stats
