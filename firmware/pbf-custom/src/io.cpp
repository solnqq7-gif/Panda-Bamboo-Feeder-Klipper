#include "io.h"

#include "config.h"
#include "settings.h"

namespace io {
namespace {

constexpr uint32_t kSensorDebounceMs = 10;
constexpr uint32_t kKeyDebounceMs = 20;
constexpr uint32_t kHoldMs = 600;
constexpr uint32_t kBothHoldMs = 1000;
constexpr uint8_t kBuzzerChannel = 0;
constexpr uint32_t kBuzzerFreq = 2700;

struct Debounced {
  int pin;
  bool stable;
  bool raw;
  uint32_t since;
};

Debounced in1{pins::kIn1}, in2{pins::kIn2}, buf_end{pins::kBufEnd}, buf_pos{pins::kBufPos};
Debounced key_up{pins::kKeyUp}, key_back{pins::kKeyBack};
Sensors current{};
volatile uint32_t buf_pos_edges = 0;

struct KeyState {
  bool down = false;
  uint32_t down_since = 0;
  bool holding = false;
};
KeyState up_state, back_state;
bool both_mode = false;  // both keys pressed: suppress single-key events until both released
bool both_fired = false;

constexpr size_t kQueueSize = 16;
KeyEvent queue[kQueueSize];
size_t q_head = 0, q_tail = 0;

// Buzzer pattern state
uint8_t beeps_left = 0;
uint16_t beep_on_ms = 0, beep_off_ms = 0;
bool buzzer_on = false;
uint32_t buzzer_next_ms = 0;
bool alarm_on = false;

void IRAM_ATTR onBufPosEdge() { buf_pos_edges = buf_pos_edges + 1; }

void push(KeyEvent e) {
  size_t next = (q_head + 1) % kQueueSize;
  if (next == q_tail) return;  // full: drop
  queue[q_head] = e;
  q_head = next;
}

bool debounce(Debounced &d, uint32_t now, uint32_t ms) {
  bool level = digitalRead(d.pin);
  if (level != d.raw) {
    d.raw = level;
    d.since = now;
  }
  if (d.raw != d.stable && now - d.since >= ms) {
    d.stable = d.raw;
    return true;
  }
  return false;
}

void initDebounced(Debounced &d, uint32_t now) {
  pinMode(d.pin, INPUT_PULLUP);
  delay(1);
  d.stable = d.raw = digitalRead(d.pin);
  d.since = now;
}

void updateKey(KeyState &k, bool down, uint32_t now, KeyEvent down_ev, KeyEvent short_ev,
               KeyEvent hold_start, KeyEvent hold_end) {
  if (down && !k.down) {
    k.down = true;
    k.down_since = now;
    k.holding = false;
    push(down_ev);
  } else if (!down && k.down) {
    k.down = false;
    if (both_mode) return;
    push(k.holding ? hold_end : short_ev);
    k.holding = false;
  } else if (down && !k.holding && !both_mode && now - k.down_since >= kHoldMs) {
    k.holding = true;
    push(hold_start);
  }
}

void buzzerWrite(bool on) {
  buzzer_on = on;
  ledcWrite(kBuzzerChannel, on ? 512 : 0);
}

void updateBuzzer(uint32_t now) {
  if (beeps_left == 0 && !alarm_on) {
    if (buzzer_on) buzzerWrite(false);
    return;
  }
  if ((int32_t)(now - buzzer_next_ms) < 0) return;
  if (buzzer_on) {
    buzzerWrite(false);
    if (beeps_left > 0) beeps_left--;
    buzzer_next_ms = now + (alarm_on && beeps_left == 0 ? 700 : beep_off_ms);
  } else {
    if (beeps_left == 0 && alarm_on) {
      beeps_left = 2;  // alarm = pairs of beeps
      beep_on_ms = 150;
      beep_off_ms = 120;
    }
    if (!settings::get().buzzer) {
      beeps_left = 0;
      return;
    }
    buzzerWrite(true);
    buzzer_next_ms = now + beep_on_ms;
  }
}

}  // namespace

void begin() {
  uint32_t now = millis();
  initDebounced(in1, now);
  initDebounced(in2, now);
  initDebounced(buf_end, now);
  initDebounced(buf_pos, now);
  initDebounced(key_up, now);
  initDebounced(key_back, now);
  attachInterrupt(digitalPinToInterrupt(pins::kBufPos), onBufPosEdge, CHANGE);
  current = {in1.stable, in2.stable, buf_end.stable, buf_pos.stable};

  ledcSetup(kBuzzerChannel, kBuzzerFreq, 10);
  ledcAttachPin(pins::kBuzzer, kBuzzerChannel);
  buzzerWrite(false);
}

void update() {
  uint32_t now = millis();
  debounce(in1, now, kSensorDebounceMs);
  debounce(in2, now, kSensorDebounceMs);
  debounce(buf_end, now, kSensorDebounceMs);
  debounce(buf_pos, now, kSensorDebounceMs);
  current = {in1.stable, in2.stable, buf_end.stable, buf_pos.stable};

  debounce(key_up, now, kKeyDebounceMs);
  debounce(key_back, now, kKeyDebounceMs);
  bool up = !key_up.stable;  // LOW = pressed
  bool back = !key_back.stable;

  if (up && back) {
    if (!both_mode) {
      both_mode = true;
      both_fired = false;
      // Cancel a hold that started before the second key went down.
      if (up_state.holding) push(KeyEvent::kUpHoldEnd);
      if (back_state.holding) push(KeyEvent::kBackHoldEnd);
      up_state.holding = back_state.holding = false;
    }
    uint32_t since = max(up_state.down ? up_state.down_since : now,
                         back_state.down ? back_state.down_since : now);
    if (!both_fired && now - since >= kBothHoldMs) {
      both_fired = true;
      push(KeyEvent::kBothLong);
    }
  }
  updateKey(up_state, up, now, KeyEvent::kUpDown, KeyEvent::kUpShort, KeyEvent::kUpHoldStart,
            KeyEvent::kUpHoldEnd);
  updateKey(back_state, back, now, KeyEvent::kBackDown, KeyEvent::kBackShort,
            KeyEvent::kBackHoldStart, KeyEvent::kBackHoldEnd);
  if (both_mode && !up && !back) both_mode = false;

  updateBuzzer(now);
}

const Sensors &sensors() { return current; }
uint32_t bufPosEdges() { return buf_pos_edges; }
void resetBufPosEdges() { buf_pos_edges = 0; }

KeyEvent nextKey() {
  if (q_tail == q_head) return KeyEvent::kNone;
  KeyEvent e = queue[q_tail];
  q_tail = (q_tail + 1) % kQueueSize;
  return e;
}

bool keyUpDown() { return up_state.down; }
bool keyBackDown() { return back_state.down; }

void beep(uint8_t count, uint16_t on_ms, uint16_t off_ms) {
  if (!settings::get().buzzer || alarm_on) return;
  beeps_left = count;
  beep_on_ms = on_ms;
  beep_off_ms = off_ms;
  buzzer_next_ms = millis();
  buzzerWrite(false);
}

void alarm(bool on) {
  alarm_on = on;
  if (!on) {
    beeps_left = 0;
    buzzerWrite(false);
  } else {
    buzzer_next_ms = millis();
  }
}

bool alarmActive() { return alarm_on; }

void testBeep() {
  buzzerWrite(true);
  delay(120);
  buzzerWrite(false);
}

}  // namespace io
