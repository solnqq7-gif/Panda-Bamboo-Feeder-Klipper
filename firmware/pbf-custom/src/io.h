// Filament/buffer sensors, the two keys and the buzzer.
#pragma once

#include <Arduino.h>

namespace io {

enum class KeyEvent {
  kNone,
  kUpDown,    // key pressed (sent before the short/hold events of the same press)
  kBackDown,
  kUpShort,
  kBackShort,
  kUpHoldStart,
  kUpHoldEnd,
  kBackHoldStart,
  kBackHoldEnd,
  kBothLong,
};

struct Sensors {
  bool in1;       // filament at the inlet
  bool in2;       // filament at the drive gear
  bool buf_rest;  // buffer carriage at its inner end (no demand)
  bool buf_pos;   // comb level
};

void begin();
void update();  // call from loop()

const Sensors &sensors();
// Edges of the buffer comb since the last reset. Counted in an ISR, so fast moves are not missed.
uint32_t bufPosEdges();
void resetBufPosEdges();

KeyEvent nextKey();
bool keyUpDown();
bool keyBackDown();

// Buzzer. All patterns respect the buzzer setting except test().
void beep(uint8_t count = 1, uint16_t on_ms = 80, uint16_t off_ms = 80);
void alarm(bool on);  // repeating alarm until alarm(false)
bool alarmActive();
void testBeep();

}  // namespace io
