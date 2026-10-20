#include "settings.h"
#include "config.h"
#include "moonraker.h"
#include "io.h"
#include "ui.h"
#include "motor.h"
#include <cerrno>
#include <math.h>
#include <iostream>
#include <vector>

int checks = 0;
void check(bool passed, const char *scenario) {
  ++checks;
  if (!passed) { std::cerr << "FAIL: " << scenario << '\n'; std::exit(1); }
}

namespace settings {
Settings current;
constexpr size_t kNameMax = 12, kSensorMax = 64;
struct Guard { ~Guard() {} };
int saves = 0;
void sanitize(Settings &s);
void save() { ++saves; sanitize(current); }
Settings &get() { return current; }
void print(Print &) {}
#include "settings_under_test.inc"
}

namespace moonraker {
Snapshot value;
std::vector<std::string> scripts;
Snapshot snapshot() { return value; }
bool isPrinting() { return value.fresh && value.print_state == PrintState::kPrinting; }
void sendGcode(const char *script, bool) { scripts.push_back(script); }
void notify(const char *, const char *) {}
}
namespace stats {
int unload_jams = 0;
void addUnloadJam() { ++unload_jams; }
}
namespace motor {
bool moving = false;
bool isMoving() { return moving; }
bool driverOk() { return true; }
void stop() { moving = false; }
void halt() { moving = false; }
void setCurrentMa(uint16_t, uint16_t) {}
float moveDistanceMm() { return 10; }
void run(Dir, float) { moving = true; }
int moves = 0;
Dir move_dir = Dir::kFeed;
float move_mm = 0, move_speed = 0;
void moveMm(Dir dir, float mm, float speed) {
  ++moves; move_dir = dir; move_mm = mm; move_speed = speed; moving = true;
}
}
namespace io {
Sensors value{true, true, false, false};
const Sensors &sensors() { return value; }
std::vector<KeyEvent> events;
KeyEvent nextKey() {
  if (events.empty()) return KeyEvent::kNone;
  auto e = events.front(); events.erase(events.begin()); return e;
}
void beep(uint8_t, uint16_t, uint16_t) {}
void alarm(bool) {}
bool keyUpDown() { return false; }
bool keyBackDown() { return false; }
}
namespace ui {
bool menu_active = false;
bool menuActive() { return menu_active; }
void menuOpen() { menu_active = true; }
MenuAction menuKey(io::KeyEvent) { return MenuAction::kNone; }
#include "ui_under_test.inc"
}

enum class State { kReady, kFree, kFeeding, kStaged, kInserting, kPreload, kLoading,
                   kUnloading, kFollowing, kInsertWait, kUnloaded, kStuck, kError,
                   kStandby, kEmpty, kTailOut, kNoDriver };
enum class PtestPhase { kOff, kHeat };
enum class BtestPhase { kOff, kDrain, kParkRefill };
State state = State::kReady;
PtestPhase ptest_phase = PtestPhase::kOff;
BtestPhase btest_phase = BtestPhase::kOff;
String ptest_msg, btest_msg, message;
float ptest_old_target = 0;
bool full_test = false, jogging = false, free_blocked = true;
bool free_from_k1 = false, free_from_retract = false;
bool up_click_pending = false, back_click_pending = false;
bool up_stop_press = false, up_click_stopped = false;
bool swallow_up = false, swallow_back = false, web_hold = false, web_hold_refused = false;
bool unload_from_click = false, back_convert_pending = false, follow_blocked_by_key = false;
uint32_t up_click_ms = 0, back_click_ms = 0, unload_click_ms = 0;
constexpr uint32_t kDoubleClickMs = 450, kClickToHoldMs = 2000;
constexpr uint32_t kFollowMaxAgeMs = 700;
constexpr float kFollowStartMmS = 3, kFollowMaxSpeed = 60;
using motor::Dir;
using std::min;
State follow_return = State::kReady;
float follow_start_pos = 0;
uint32_t topup_block_until_ms = 0, free_quiet_ms = 0, free_until_ms = 60000, last_wake_ms = 0;
constexpr float kFollowStopMmS = 0.5f;
constexpr uint32_t kFreeQuietMs = 3000;
const Settings &cfg() { return settings::get(); }
const char *pn() { return "Printer"; }
const char *tr(const char *zh, const char *) { return zh; }
uint32_t topUpBlockRemainingMs(uint32_t) { return 0; }
void logf(const char *, ...) {}
bool canFree() { return !moonraker::isPrinting(); }
void enterFree(const String &, bool from) { state = State::kFree; free_from_k1 = from; }
void exitFree(const String &, bool block) { state = State::kReady; free_blocked = block; }
bool ptestActive() { return ptest_phase != PtestPhase::kOff; }
bool btestActive() { return btest_phase != BtestPhase::kOff; }
bool testActive() { return ptestActive() || btestActive(); }
bool btestOwnsMotor() { return btestActive() && btest_phase != BtestPhase::kParkRefill; }
void startPressureTest() {} void startBufferTest() {} void startFullTest() {}
void applyPressure() {}
void applyMovePressure() {}
void enter(State s, const String &msg = "") { state = s; message = msg; }
void enterAlarm(State s, const String &msg) { enter(s, msg); }
void settle() { enter(io::value.in2 ? State::kReady : State::kEmpty); }
void exitStandby(const String &) { enter(State::kReady); }
int loads = 0, unloads = 0, holds = 0;
void startLoad(float, bool) { ++loads; state = State::kLoading; motor::moving = true; }
void startUnload(float, bool) { ++unloads; state = State::kUnloading; motor::moving = true; }
void startInsert() { state = State::kInserting; motor::moving = true; }
void startKeyHold(bool) { ++holds; jogging = true; motor::moving = true; }
float test_pos = 0;
float pos() { return test_pos; }
float unload_start_pos = 0, unload_limit_mm = 0, unload_gear_pos = 0;
bool unload_full = false, unload_from_loaded = false, unload_past_gear = false;
constexpr float kUnloadExtraMm = 15, kUnloadMarginMm = 300, kUnloadLearnExtraMm = 20;
void fullTestFinish(bool, const char *, const String &) {}
void testResultFinish(bool, const char *, const String &) {}
bool heater_off = false;
bool printerHeaterOff() { return heater_off; }
bool relaxing = false;
uint8_t relax_count = 0;
uint32_t relax_wait_ms = 0;
float relax_start_pos = 0;
constexpr float kRelaxSpeed = 10, kRelaxMarginMm = 6;
constexpr uint32_t kRelaxSettleMs = 2000;
constexpr uint8_t kRelaxMax = 3;
#include "feeder_under_test.inc"

namespace logbuf {
void line(const char *) {}
void appendJsonString(String &out, const char *s) { out = out + "\"" + s + "\""; }
}
struct Server {
  std::vector<std::pair<String, String>> fields;
  int status = 0;
  int args() { return (int)fields.size(); }
  String argName(int i) { return fields[i].first; }
  String arg(int i) { return fields[i].second; }
  void send(int code, const char *, const String &) { status = code; }
} server;
void handleSettingsGet() { server.status = 200; }
#include "net_under_test.inc"

void settingsCases() {
  settings::current = Settings();
  for (bool persist : {false, true}) {
    for (const char *bad : {"-1", "65536", "999999999999999999999999", "", "abc", "12mm", "1.5"}) {
      auto old = settings::get().load_mm;
      int saves = settings::saves;
      check(!settings::set("load_mm", bad, persist), "reject invalid integer");
      check(settings::get().load_mm == old && settings::saves == saves, "rejection preserves value and flash");
    }
    check(!settings::set("pressure", "-1", persist), "negative pressure does not select L10");
    check(!settings::set("pressure", "257", persist), "8-bit overflow is rejected before narrowing");
    for (const char *bad : {"nan", "NaN", "inf", "-inf", "1e999", "250junk", "", "49", "1001"}) {
      check(!settings::set("steps_mm", bad, persist), "reject invalid floating-point setting");
      check(settings::get().steps_per_mm == 250, "invalid float preserves calibration");
    }
  }
  for (const char *good : {"1", "10000", " 650 "})
    check(settings::set("load_mm", good, false), "accept valid distances and whitespace");
  check(settings::get().load_mm == 650, "valid distance applied before persistence");
  check(settings::set("steps_mm", "261.25", false), "fractional calibration accepted");
  check(settings::get().steps_per_mm == 261.25f, "fractional calibration retained");
  check(settings::set("nofeed_mm", "0", false), "zero disables no-feed watch");
  check(!settings::set("nofeed_mm", "1", false), "no-feed threshold gap rejected");
  check(settings::set("nofeed_mm", "30", false), "no-feed minimum accepted");
  check(settings::set("english", "1", false) && settings::get().english, "English can be chosen");
  check(settings::set("english", "0", false) && !settings::get().english, "and Chinese again");
  check(settings::set("move_level", "0", false), "zero move level keeps inheritance");
  check(settings::set("printer_port", "65535", false), "port upper bound accepted");
  check(!settings::set("printer_port", "65536", false), "overflowing port rejected");
  check(settings::validate("load_mm", "900") && settings::get().load_mm == 650,
        "validation does not mutate live settings");
  check(!settings::validate("not_a_setting", "1"), "unknown setting rejected");
  settings::get().steps_per_mm = NAN;
  settings::save();
  check(settings::get().steps_per_mm == 250, "stored non-finite calibration repaired");

  settings::current = Settings();
  int saves = settings::saves;
  server.fields = {{"pressure", "9"}, {"load_mm", "-1"}};
  handleSettingsPost();
  check(server.status == 400 && settings::get().pressure_level == 7 && settings::saves == saves,
        "invalid web batch rejected without partial changes or flash writes");
  server.fields = {{"pressure", "9"}, {"load_mm", "700"}};
  handleSettingsPost();
  check(server.status == 200 && settings::get().pressure_level == 9 && settings::get().load_mm == 700,
        "valid web batch applied");
  check(settings::saves == saves + 1, "valid web batch persists once");
}

void cleanupCases() {
  using moonraker::PrintState;
  settings::current = Settings();
  for (auto ps : {PrintState::kPrinting, PrintState::kPaused}) {
    for (bool fresh : {false, true}) {
      moonraker::value = {};
      moonraker::value.print_state = ps;
      moonraker::value.fresh = fresh;
      moonraker::value.nozzle_target = 220;
      moonraker::scripts.clear();
      ptest_phase = PtestPhase::kHeat;
      state = State::kFeeding;
      motor::moving = true;
      ptestEnd("print took over", false, "k1_printing");
      check(moonraker::scripts.empty(), "pressure cleanup does not override active print, including stale status");
      check(!ptestActive() && !motor::moving && state == State::kReady, "local pressure test still stops");
      btest_phase = BtestPhase::kDrain;
      motor::moving = true;
      btestEnd("print took over", false, "k1_printing");
      check(moonraker::scripts.empty() && !btestActive() && !motor::moving, "buffer cleanup leaves printer motor alone");
    }
    moonraker::value.fresh = true;
    check(std::string(testRefusal()) == "k1_printing", "printing and paused jobs refuse new tests");
  }
  moonraker::value.print_state = PrintState::kStandby;
  ptest_phase = PtestPhase::kHeat;
  moonraker::scripts.clear();
  ptestEnd("normal finish", true, "");
  check(moonraker::scripts == std::vector<std::string>{"M104 S0", "SET_STEPPER_ENABLE STEPPER=extruder ENABLE=0"},
        "idle test still restores nozzle target and releases printer motor");
}

void menuCases() {
  settings::current = Settings();
  for (bool lock : {false, true}) {
    settings::get().lock_printing = lock;
    moonraker::value.print_state = moonraker::PrintState::kPrinting;
    ui::menu_active = false;
    io::events = {io::KeyEvent::kBothLong};
    handleKeys();
    check(!ui::menuActive(), "printing refuses menu regardless of optional move lock");
    ui::menuOpen();
    back_click_pending = true;
    int saves = settings::saves;
    handleKeys();
    check(!ui::menuActive() && settings::saves == saves + 1 && !back_click_pending,
          "print arriving with menu open closes and saves without a key event");
  }
  moonraker::value.print_state = moonraker::PrintState::kStandby;
  io::events = {io::KeyEvent::kBothLong};
  handleKeys();
  check(ui::menuActive(), "idle menu still opens");
  ui::menuClose();
}

void freeCases() {
  settings::current = Settings();
  moonraker::value = {};
  state = State::kReady;
  free_blocked = true;
  freeTick(1000);
  check(free_blocked, "boot without a snapshot does not arm FREE");
  auto &s = moonraker::value;
  s.fresh = s.klippy_ready = s.pbf_display = true;
  s.print_state = moonraker::PrintState::kStandby;
  s.pbf_mode = 1;
  freeTick(1001);
  check(state == State::kReady && free_blocked, "residual FREE on first snapshot is ignored");
  s.pbf_mode = 0;
  freeTick(1002);
  check(!free_blocked, "valid cleared marker arms FREE");
  s.pbf_mode = 1;
  freeTick(1003);
  check(state == State::kFree, "new FREE after clear releases gear");
  s.fresh = false;
  freeTick(1004);
  check(state == State::kReady && free_blocked, "disconnect grips gear and latches block");
  s.fresh = true;
  freeTick(1005);
  check(state == State::kReady && free_blocked, "same FREE after reconnect does not release again");
  s.pbf_mode = 0;
  s.klippy_ready = false;
  freeTick(1006);
  check(free_blocked, "Klippy not ready cannot clear block");
  s.klippy_ready = true;
  s.pbf_display = false;
  freeTick(1007);
  check(free_blocked, "missing signal objects cannot clear block");
  s.pbf_display = true;
  freeTick(1008);
  s.pbf_mode = 1;
  freeTick(1009);
  check(state == State::kFree, "clear then fresh request works after reconnect");
  free_until_ms = 1010;
  freeTick(1011);
  check(state == State::kReady && free_blocked, "timeout grips and blocks same marker");
  s.fresh = false;
  freeTick(1012);
  s.fresh = true;
  freeTick(1013);
  check(state == State::kReady && free_blocked, "disconnect cannot undo timeout/manual latch");
}

void resetKeys(State initial) {
  settings::current = Settings();
  moonraker::value = {};
  moonraker::value.fresh = true;
  moonraker::value.print_state = moonraker::PrintState::kStandby;
  io::value = {true, true, false, false};
  state = initial;
  ptest_phase = PtestPhase::kOff;
  btest_phase = BtestPhase::kOff;
  up_click_pending = back_click_pending = up_stop_press = up_click_stopped = false;
  swallow_up = swallow_back = jogging = web_hold = web_hold_refused = false;
  unload_from_click = back_convert_pending = follow_blocked_by_key = false;
  motor::moving = false;
  follow_return = State::kReady;
  loads = unloads = holds = 0;
  test_now = 10000;
}

void upTap() {
  handleKey(io::KeyEvent::kUpDown);
  test_now += 50;
  handleKey(io::KeyEvent::kUpShort);
  test_now += 100;
}

void keyCases() {
  for (State initial : {State::kLoading, State::kUnloading, State::kFollowing, State::kReady}) {
    for (bool paused : {false, true}) {
      resetKeys(initial);
      if (paused) moonraker::value.print_state = moonraker::PrintState::kPaused;
      motor::moving = true;
      if (initial == State::kReady) jogging = web_hold = true;
      upTap();
      check(!motor::moving, "first UP press immediately stops a manual move");
      check(up_click_pending && up_click_stopped, "stop click counts toward double UP");
      if (initial == State::kReady) check(web_hold_refused, "old web keep-alive cannot restart stopped move");
      upTap();
      check(state == State::kStaged && !motor::moving && !jogging && !web_hold,
            "two UP taps during feed/retract/follow/hold reach STAGED");
      test_now += 1000;
      serviceClicks();
      check(state == State::kStaged && loads == 0 && unloads == 0, "no delayed load after double UP");
    }
  }
  resetKeys(State::kUnloading);
  motor::moving = true;
  upTap();
  test_now += kDoubleClickMs + 1;
  serviceClicks();
  check(!up_click_pending && loads == 0 && !motor::moving, "single stop never becomes a load after timeout");

  resetKeys(State::kReady);
  upTap(); upTap();
  check(state == State::kStaged, "existing READY double click still stages");

  resetKeys(State::kStaged);
  upTap();
  check(loads == 1 && state == State::kLoading, "ordinary STAGED single UP still loads immediately");

  resetKeys(State::kLoading);
  motor::moving = true;
  handleKey(io::KeyEvent::kUpDown);
  test_now += 650;
  handleKey(io::KeyEvent::kUpHoldStart);
  handleKey(io::KeyEvent::kUpHoldEnd);
  check(!up_stop_press && !up_click_pending && holds == 0, "long stopping press cannot count as double click or jog");

  resetKeys(State::kLoading);
  motor::moving = true;
  handleKey(io::KeyEvent::kUpDown);
  handleKey(io::KeyEvent::kBackDown);
  // io.cpp suppresses the individual release events after a two-key combination.
  serviceClicks();
  check(!up_stop_press && !swallow_up && !up_click_pending,
        "two-key gesture with suppressed release cannot leave buffer waiting forever");

  resetKeys(State::kReady);
  moonraker::value.print_state = moonraker::PrintState::kPrinting;
  upTap(); upTap();
  check(state == State::kReady && !up_click_pending, "double UP cannot pause buffer during a locked print");

  resetKeys(State::kUnloading);
  motor::moving = true;
  upTap();
  moonraker::value.print_state = moonraker::PrintState::kPrinting;
  serviceClicks();
  check(!up_click_pending && !up_stop_press, "print takeover clears pending stop gesture without a key event");

  resetKeys(State::kFollowing);
  motor::moving = true;
  moonraker::value.pbf_mode = 2;
  moonraker::value.extruder_velocity = 5;
  upTap(); upTap();
  check(!maybeStartFollowing() && state == State::kStaged, "same printer move cannot undo manual STAGED");
  moonraker::value.extruder_velocity = 0;
  check(!maybeStartFollowing(), "printer stop rearms follow without movement");
  moonraker::value.extruder_velocity = -5;
  check(maybeStartFollowing(), "next printer move can follow normally");
}

void beginUnload(float limit, bool full, bool from_loaded) {
  settings::current = Settings();
  io::value = {true, true, false, false};
  state = State::kUnloading;
  motor::moving = true;
  unload_start_pos = test_pos = 0;
  unload_limit_mm = limit;
  unload_full = full;
  unload_from_loaded = from_loaded;
  unload_past_gear = false;
}

void unloadTo(float moved, bool in2) {
  io::value.in2 = in2;
  test_pos = -moved;
  updateUnloading(io::value);
}

void unloadCases() {
  settings::current = Settings();
  settings::get().unload_measured_mm = 500;
  check(fullUnloadLimit() == 520, "learned full unload limit is the measured length + 20 mm");
  settings::get().lock_move_distance = true;
  check(fullUnloadLimit() == settings::get().unload_mm, "locked distances use the unload setting");
  settings::get().lock_move_distance = false;
  settings::get().unload_measured_mm = 0;
  check(fullUnloadLimit() == settings::get().preload_mm + pathMm() + kUnloadMarginMm,
        "unmeasured path keeps the estimate");

  beginUnload(520, true, false);
  unloadTo(510, true);
  unloadTo(511, false);  // the tip clears the gear 9 mm short of the limit
  unloadTo(521, false);
  check(state == State::kUnloading && motor::moving, "limit does not cut the pull past the gear short");
  unloadTo(526, false);
  check(state == State::kUnloaded && !motor::moving && message == "pull out or reload",
        "full unload still finishes 15 mm past the gear");

  beginUnload(520, true, false);
  int jams = stats::unload_jams;
  unloadTo(519, true);
  check(state == State::kUnloading && motor::moving, "no jam before the limit");
  unloadTo(520, true);
  check(state == State::kError && !motor::moving && stats::unload_jams == jams + 1,
        "tip still in the gear at the limit is a jam");

  beginUnload(1000, true, true);
  settings::get().unload_measured_mm = 500;
  unloadTo(480, false);
  check(settings::get().unload_measured_mm == 480 && fullUnloadLimit() == 500,
        "unload from the printer relearns the limit");

  beginUnload(600, false, false);
  unloadTo(600, true);
  check(state == State::kStaged && !motor::moving, "distance unload still stops at its distance");
  test_pos = 0;
}

void resetRelax(bool heater_is_off) {
  settings::current = Settings();
  settings::get().overfeed_mm = 15;
  io::value = {true, true, false, false};  // carriage out: the buffer holds its parked slack
  state = State::kReady;
  ptest_phase = PtestPhase::kOff;
  btest_phase = BtestPhase::kOff;
  jogging = web_hold = false;
  motor::moving = false;
  motor::moves = 0;
  heater_off = heater_is_off;
  relaxing = false;
  relax_count = 0;
  test_now = 10000;
  relax_wait_ms = test_now;
  test_pos = 0;
  message = "";
}

void relaxAt(uint32_t dt, bool home, float pulled) {
  test_now += dt;
  io::value.buf_rest = home;
  test_pos = -pulled;
  relaxTick(io::value, test_now);
}

// Starts a relax from a settled, cold, parked buffer and checks that it did start.
void startRelax() {
  int moves = motor::moves;
  relaxAt(kRelaxSettleMs, false, 0);
  check(relaxing && motor::moves == moves + 1, "settled cold buffer starts a relax");
}

void relaxCases() {
  resetRelax(false);
  relaxAt(5000, false, 0);
  check(motor::moves == 0 && !relaxing, "heater on: the parked buffer stays as it is");
  heater_off = true;
  relaxAt(100, false, 0);
  relaxAt(kRelaxSettleMs - 200, false, 0);
  check(motor::moves == 0, "heater off: waits for the carriage to settle first");
  relaxAt(200, false, 0);
  check(relaxing && motor::moves == 1 && motor::move_dir == Dir::kRetract &&
            motor::move_mm == 15 + kRelaxMarginMm && motor::move_speed == kRelaxSpeed,
        "pulls back slowly, at most the parking amount + margin");
  relaxAt(500, false, 8);
  check(relaxing && motor::moving && message == "relaxing buffer 8mm", "keeps pulling while out");
  relaxAt(500, true, 14);
  check(!relaxing && !motor::moving && message == "Printer heater off",
        "stops the moment the carriage is home");
  relaxAt(5000, true, 14);
  check(motor::moves == 1, "a home carriage is never pulled further");

  // The end-of-print retract and head moves can push the carriage out again.
  for (int round = 2; round <= kRelaxMax; ++round) {
    relaxAt(100, false, 14);
    check(!relaxing, "a carriage pushed out again settles before the next relax");
    startRelax();
    relaxAt(500, true, 17);
  }
  relaxAt(100, false, 17);
  relaxAt(10000, false, 17);
  check(motor::moves == kRelaxMax && !relaxing, "at most kRelaxMax relaxes per heater-off stretch");
  heater_off = false;
  relaxAt(100, false, 17);
  heater_off = true;
  relaxAt(100, false, 17);
  startRelax();
  check(relax_count == 1, "heating up starts a new stretch");
  heater_off = false;
  relaxAt(100, false, 20);
  check(!relaxing && !motor::moving, "heating again stops a relax at once");

  resetRelax(true);
  startRelax();
  motor::moving = false;  // the whole distance ran out with the carriage still out
  relaxAt(3000, false, 15 + kRelaxMarginMm);
  relaxAt(10000, false, 15 + kRelaxMarginMm);
  check(!relaxing && motor::moves == 1, "a relax that ran its whole distance is not repeated");

  resetRelax(true);
  startRelax();
  motor::moving = false;  // a key press stopped it
  relaxAt(100, false, 5);
  relaxAt(10000, false, 5);
  check(!relaxing && motor::moves == 1, "a relax stopped by a key stays stopped until reheating");

  for (int busy = 0; busy < 4; ++busy) {
    resetRelax(true);
    jogging = busy == 0;
    web_hold = busy == 1;
    if (busy == 2) ptest_phase = PtestPhase::kHeat;
    motor::moving = busy == 3;
    relaxAt(10000, false, 0);
    check(motor::moves == 0 && !relaxing, "no relax during jogs, web holds, tests or own moves");
  }
  resetRelax(true);
  relaxAt(10000, true, 0);
  check(motor::moves == 0, "an empty buffer is left alone");
  test_pos = 0;
}

// READY's message stays on screen through every top-up, so a stale hold text would last a print.
void holdMessageCases() {
  moonraker::value = moonraker::Snapshot();
  heater_off = true;
  message = "Printer heater off";
  endHoldMessage(false);
  check(message == "Printer heater off", "heater message stays while the heater is off");
  heater_off = false;
  endHoldMessage(false);
  check(message == "buffer on", "heater message goes when the printer heats again");
  moonraker::value.fresh = true;
  moonraker::value.print_state = moonraker::PrintState::kPrinting;
  message = "Printer heater off";
  endHoldMessage(false);
  check(message == "Printer printing: buffer on", "a print that heats up says the buffer is on");
  message = "relaxing buffer 8mm";
  endHoldMessage(false);
  check(message == "Printer printing: buffer on", "a relax message goes on reheating too");
  for (const char *anchor : {"waiting for Printer", "Printer extruder empty"}) {
    message = anchor;
    endHoldMessage(false);
    check(message == anchor, "an anchor hold keeps its message until a top-up starts");
    endHoldMessage(true);
    check(message == "Printer printing: buffer on", "a starting top-up ends the anchor message");
  }
  message = "loaded 1040mm (at extruder)";
  endHoldMessage(true);
  check(message == "loaded 1040mm (at extruder)", "other READY messages are left alone");
  moonraker::value = moonraker::Snapshot();
}

int main() {
  settingsCases(); cleanupCases(); menuCases(); freeCases(); keyCases(); unloadCases();
  relaxCases(); holdMessageCases();
  std::cout << "PASS: " << checks
            << " regression checks (settings/API, cleanup, menu, FREE, manual keys, unload limit, "
               "cold relax, hold messages)\n";
}
