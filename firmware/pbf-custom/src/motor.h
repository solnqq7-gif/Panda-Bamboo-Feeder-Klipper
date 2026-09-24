// Stepper control: TMC2209 over UART + step pulses from a fixed-rate timer with a phase
// accumulator, so the speed can ramp without reprogramming the timer.
#pragma once

#include <Arduino.h>

namespace motor {

enum class Dir { kFeed, kRetract };

bool begin();                 // false if the driver does not answer on UART
bool driverOk();
// Run current and the standstill current, both in mA (the standstill one is capped at the run
// current, since the driver expresses it as a fraction).
void setCurrentMa(uint16_t ma, uint16_t hold_ma);
uint16_t holdMa();
uint16_t currentMa();

// Runs until stop()/halt(). Speed can be changed while running.
void run(Dir dir, float mm_per_s);
// Moves a fixed distance, decelerating to stop at the end.
void moveMm(Dir dir, float mm, float mm_per_s);
void setSpeed(float mm_per_s);
void stop();   // decelerate to a stop
void halt();   // stop immediately
void release();  // driver off (motor free-wheels)
void hold();     // driver on without moving: the gear resists the printer pulling filament
bool isHolding();

bool isMoving();
Dir direction();
// Signed distance since boot in mm, + = feed direction.
float positionMm();
// Distance travelled by the current/last move in mm (always positive).
float moveDistanceMm();

void update();  // call from loop()

// Diagnostics
uint32_t drvStatus();
uint16_t sgResult();
// Failed UART reads that recovered by themselves: interference, not a power loss.
uint32_t uartHiccups();
bool driverWarm();  // TMC over temperature warning or shutdown
String describeStatus();

}  // namespace motor
