#include "motor.h"

#include <TMCStepper.h>

#include "config.h"
#include "logbuf.h"
#include "settings.h"
#include "soc/gpio_reg.h"

namespace motor {
namespace {

constexpr uint32_t kTickHz = 40000;                      // step ISR rate, max 40k steps/s
constexpr float kPhasePerSps = 4294967296.0f / kTickHz;  // 2^32 / tick rate
constexpr float kAccelMmS2 = 300.0f;
constexpr float kStartSpeedMmS = 5.0f;
constexpr float kMinSpeedMmS = 1.0f;
constexpr uint32_t kDriverCheckMs = 2000;
constexpr uint8_t kDriverFailsToTrip = 3;  // consecutive failed reads before calling it a power loss
constexpr uint32_t kGconfPdnDisable = 1u << 6;

TMC2209Stepper driver(&Serial1, hw::kRSense, hw::kDriverAddr);
hw_timer_t *timer = nullptr;

// Shared with the ISR
volatile bool running = false;
volatile bool pulse_high = false;
volatile uint32_t phase = 0;
volatile uint32_t phase_inc = 0;
volatile int32_t remaining = -1;  // steps left for a fixed move, -1 = unlimited
volatile int32_t pos_steps = 0;
volatile int8_t step_sign = 1;

bool driver_ok = false;
uint8_t driver_fails = 0;      // consecutive failed reads
uint32_t uart_hiccups = 0;     // failed reads that recovered, i.e. interference rather than power
bool driver_warm = false;      // TMC over temperature warning or shutdown is latched
bool enabled = false;
uint16_t current_ma = 0;
uint16_t hold_ma_set = 300;  // standstill current in mA, capped at current_ma
Dir cur_dir = Dir::kFeed;
float target_mm_s = 0;
float speed_mm_s = 0;
bool stopping = false;
int32_t move_start_steps = 0;
uint32_t last_update_us = 0;
uint32_t last_driver_check_ms = 0;

void IRAM_ATTR onTick() {
  if (pulse_high) {
    REG_WRITE(GPIO_OUT_W1TC_REG, BIT(pins::kStep));
    pulse_high = false;
  }
  if (!running) return;
  uint32_t prev = phase;
  phase = prev + phase_inc;
  if (phase >= prev) return;  // accumulator did not wrap: no step this tick
  if (remaining == 0) {
    running = false;
    return;
  }
  REG_WRITE(GPIO_OUT_W1TS_REG, BIT(pins::kStep));
  pulse_high = true;
  pos_steps = pos_steps + step_sign;
  if (remaining > 0) remaining = remaining - 1;
}

float stepsPerMm() { return settings::get().steps_per_mm; }

void applySpeed(float mm_s) {
  speed_mm_s = mm_s;
  phase_inc = (uint32_t)(mm_s * stepsPerMm() * kPhasePerSps);
}

void enable() {
  if (enabled) return;
  digitalWrite(pins::kEn, LOW);
  enabled = true;
  delay(2);
}

void configureDriver() {
  driver.begin();                // pdn_disable, mstep_reg_select
  driver.I_scale_analog(false);  // current from registers, not the VREF pin
  driver.toff(5);
  driver.blank_time(24);
  driver.microsteps(hw::kMicrosteps);
  driver.en_spreadCycle(false);
  driver.pwm_autoscale(true);
  driver.TCOOLTHRS(0xFFFFF);  // keep SG_RESULT valid at all speeds (logged for tuning)
  if (current_ma) driver.rms_current(current_ma, min(hold_ma_set, current_ma) / (float)current_ma);
}

void start(Dir dir, float mm_s, int32_t steps) {
  halt();
  if (!driver_ok) return;
  cur_dir = dir;
  bool feed_level = !settings::get().invert_dir;
  digitalWrite(pins::kDir, dir == Dir::kFeed ? feed_level : !feed_level);
  step_sign = dir == Dir::kFeed ? 1 : -1;
  enable();
  move_start_steps = pos_steps;
  target_mm_s = mm_s;
  stopping = false;
  phase = 0;
  applySpeed(min(mm_s, kStartSpeedMmS));
  remaining = steps;
  last_update_us = micros();
  running = true;
}

// If the 24V supply was cycled the driver lost its configuration: detect and restore it.
void checkDriver() {
  uint32_t now = millis();
  if (now - last_driver_check_ms < kDriverCheckMs) return;
  last_driver_check_ms = now;
  // A single failed read is almost always interference from the printer on the UART line, not a
  // power loss - during a print it happened a few times a minute. Dropping out of READY for each
  // one stops the buffer mid top-up, so only several failures in a row count as a real problem.
  bool answering = driver.test_connection() == 0 || driver.test_connection() == 0;
  if (!answering) {
    uart_hiccups++;
    if (driver_fails < 255) driver_fails++;
    if (driver_fails < kDriverFailsToTrip) return;
    if (driver_ok) {
      logbuf::logf("motor: driver did not answer %u checks in a row (24V off?)", driver_fails);
    }
    driver_ok = false;
    return;
  }
  driver_fails = 0;
  if (!driver_ok || !(driver.GCONF() & kGconfPdnDisable)) {
    logbuf::line("motor: driver (re)configured");
    configureDriver();
    driver_ok = true;
  }
  // Holding the gear at standstill all print long heats the driver: say so instead of just
  // losing torque silently.
  bool warm = driver.DRV_STATUS() & ((1u << 0) | (1u << 1));
  if (warm != driver_warm) {
    driver_warm = warm;
    logbuf::logf("motor: driver %s", warm ? "OVER TEMPERATURE - lower the pressure level or the "
                                            "standstill current"
                                          : "temperature back to normal");
  }
}

}  // namespace

bool begin() {
  digitalWrite(pins::kEn, HIGH);
  pinMode(pins::kEn, OUTPUT);
  digitalWrite(pins::kStep, LOW);
  pinMode(pins::kStep, OUTPUT);
  digitalWrite(pins::kDir, HIGH);
  pinMode(pins::kDir, OUTPUT);

  timer = timerBegin(0, 80, true);  // 1 MHz
  timerAttachInterrupt(timer, &onTick, true);
  timerAlarmWrite(timer, 1000000 / kTickHz, true);
  timerAlarmEnable(timer);

  Serial1.begin(hw::kUartBaud, SERIAL_8N1, pins::kUartRx, pins::kUartTx);
  delay(20);
  current_ma = hw::pressureLevelToMa(settings::get().pressure_level);
  hold_ma_set = settings::get().hold_ma;
  driver_ok = driver.test_connection() == 0;
  if (driver_ok) configureDriver();
  last_driver_check_ms = millis();
  return driver_ok;
}

bool driverOk() { return driver_ok; }

void setCurrentMa(uint16_t ma, uint16_t hold_ma) {
  current_ma = ma;
  hold_ma_set = hold_ma;
  // The standstill current decides how well the gear resists the printer pulling filament through.
  if (driver_ok && ma) driver.rms_current(ma, min(hold_ma_set, ma) / (float)ma);
}

uint16_t currentMa() { return current_ma; }
uint16_t holdMa() { return min(hold_ma_set, current_ma); }

void run(Dir dir, float mm_per_s) { start(dir, mm_per_s, -1); }

void moveMm(Dir dir, float mm, float mm_per_s) {
  int32_t steps = (int32_t)(mm * stepsPerMm() + 0.5f);
  if (steps <= 0) return;
  start(dir, mm_per_s, steps);
}

void setSpeed(float mm_per_s) { target_mm_s = mm_per_s; }

void stop() {
  if (running) stopping = true;
}

void halt() {
  running = false;
  phase_inc = 0;
  speed_mm_s = 0;
  stopping = false;
}

void release() {
  if (!enabled && !running) return;
  halt();
  digitalWrite(pins::kEn, HIGH);
  enabled = false;
}

void hold() {
  if (!driver_ok || enabled) return;
  enable();
}

bool isHolding() { return enabled; }

bool isMoving() { return running; }
Dir direction() { return cur_dir; }
float positionMm() { return pos_steps / stepsPerMm(); }
float moveDistanceMm() { return abs(pos_steps - move_start_steps) / stepsPerMm(); }

void update() {
  if (!running) {
    speed_mm_s = 0;
    checkDriver();
    return;
  }
  uint32_t now = micros();
  float dt = min((now - last_update_us) / 1e6f, 0.05f);
  last_update_us = now;

  float target = stopping ? 0 : target_mm_s;
  int32_t rem = remaining;
  if (rem >= 0) {
    float stop_dist = speed_mm_s * speed_mm_s / (2 * kAccelMmS2);
    if (rem / stepsPerMm() <= stop_dist) target = 0;
  }
  float v = speed_mm_s;
  if (v < target) v = min(target, v + kAccelMmS2 * dt);
  else if (v > target) v = max(target, v - kAccelMmS2 * dt);
  if (target == 0 && v <= kMinSpeedMmS) {
    if (rem < 0 || stopping) {
      halt();  // unlimited run or stop(): done
      return;
    }
    v = kMinSpeedMmS;  // fixed move: crawl to the exact end
  }
  applySpeed(v);
}

uint32_t drvStatus() { return driver_ok ? driver.DRV_STATUS() : 0; }
uint32_t uartHiccups() { return uart_hiccups; }
bool driverWarm() { return driver_warm; }
uint16_t sgResult() { return driver_ok ? driver.SG_RESULT() : 0; }

String describeStatus() {
  if (!driver_ok) return "driver not answering";
  uint32_t s = driver.DRV_STATUS();
  String out = "cs=" + String((s >> 16) & 0x1F);
  if (s & (1u << 31)) out += " standstill";
  if (s & (1u << 0)) out += " OT_WARN";
  if (s & (1u << 1)) out += " OVERTEMP";
  if (s & ((1u << 2) | (1u << 3))) out += " SHORT_GND";
  if (s & ((1u << 4) | (1u << 5))) out += " SHORT_VS";
  if (s & ((1u << 6) | (1u << 7))) out += " open_load";
  out += " I=" + String(current_ma) + "mA";
  return out;
}

}  // namespace motor
