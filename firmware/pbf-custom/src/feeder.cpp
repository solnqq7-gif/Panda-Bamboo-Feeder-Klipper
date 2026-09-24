#include "feeder.h"

#include "config.h"
#include "io.h"
#include "logbuf.h"
#include "moonraker.h"
#include "motor.h"
#include "settings.h"
#include "stats.h"
#include "ui.h"

namespace feeder {
namespace {

using motor::Dir;

enum class State : uint8_t {
  kNoDriver,
  kEmpty,
  kInserting,   // filament at the inlet, motor turning slowly so the gear can grab it
  kInsertWait,  // gave up grabbing, waiting for the user to push further
  kPreload,     // feeding preload_mm after the gear grabbed the filament
  kStaged,      // step 1 done, waiting for the UP key
  kLoading,     // step 2: feeding load_mm towards the printer
  kReady,       // buffer mode, idle
  kStandby,     // READY with the gear let go: the K1 is idle and cold, nothing can pull
  kFeeding,     // buffer mode, carriage back home (buffer empty) -> feeding
  kFollowing,   // following K1 E motion (explicit FOLLOW is bidirectional)
  kFree,        // driver off on purpose: the printer is pulling the filament itself
  kTailOut,     // runout: tail left the drive gear, the printer handles the rest
  kUnloading,
  kUnloaded,
  kStuck,
  kError,
};

const char *stateName(State s) {
  switch (s) {
    case State::kNoDriver: return "NO POWER";
    case State::kEmpty: return "EMPTY";
    case State::kInserting: return "INSERTING";
    case State::kInsertWait: return "PUSH IN";
    case State::kPreload: return "PRELOAD";
    case State::kStaged: return "STAGED";
    case State::kLoading: return "LOADING";
    case State::kReady: return "READY";
    case State::kStandby: return "STANDBY";
    case State::kFeeding: return "FEEDING";
    case State::kFollowing: return "FOLLOW";
    case State::kFree: return "FREE";
    case State::kTailOut: return "RUNOUT";
    case State::kUnloading: return "UNLOADING";
    case State::kUnloaded: return "UNLOADED";
    case State::kStuck: return "STUCK!";
    case State::kError: return "ERROR";
  }
  return "?";
}

const char *hintFor(State s) {
  switch (s) {
    case State::kNoDriver: return "connect the 24V adapter";
    case State::kEmpty: return "insert filament";
    case State::kInsertWait: return "push in / UP: retry";
    case State::kStaged: return "UP:feed BACK:ret buf-off";
    case State::kReady:
    case State::kFeeding: return "2xUP:STAGED";  // the screen puts the IP in front
    case State::kTailOut: return "insert new filament";
    case State::kUnloaded: return "UP:reload";
    case State::kFree: return "any key: grip again";
    case State::kStandby: return "any key: wake up";
    case State::kStuck:
    case State::kError: return "any key: clear alarm";
    default: return "any key: stop";
  }
}

// ---- Tuning ----
constexpr float kInsertSpeed = 8.0f;
constexpr uint32_t kInsertTimeoutMs = 15000;
constexpr float kPreloadSpeed = 15.0f;
constexpr float kSoftLoadMm = 40.0f;   // last part of a load, slow, to meet the extruder gently
constexpr float kSoftLoadSpeed = 8.0f;
constexpr float kOverfeedStallMm = 3.0f;  // no carriage movement over this much overfeed = at the end
constexpr uint32_t kBoostAfterMs = 1000;
constexpr float kBoostFactor = 1.5f;
constexpr float kJogSpeed = 15.0f;
constexpr float kWebHoldSpeed = 20.0f;
constexpr float kLoadedHoldMaxMm = 20.0f;  // filament already at the printer: each hold feeds at most this
constexpr uint32_t kWebHoldTimeoutMs = 600;  // web page re-sends "hold" every 250 ms while pressed
constexpr uint32_t kDoubleClickMs = 450;
constexpr uint32_t kClickToHoldMs = 2000;    // after a BACK click, holding within this turns into a hold
constexpr uint32_t kActiveBrightMs = 5000;   // bright after buffer feeding from READY
constexpr uint32_t kWakeBrightMs = 10000;    // normal brightness after a key press / web action in READY
constexpr float kFollowStartMmS = 3.0f;    // K1 E motion faster than this: start following it
constexpr float kFollowStopMmS = 0.5f;
constexpr uint32_t kFollowMaxAgeMs = 700;  // K1 data older than this: stop following
constexpr float kFollowMaxSpeed = 60.0f;
constexpr float kUnloadFollowMm = 20.0f;      // following back this far is an unload, not a retraction
constexpr uint32_t kFollowWindowMs = 15000;   // retractions this close together count as one unload
constexpr uint32_t kFreeQuietMs = 3000;       // no retraction for this long ends a released-by-
                                              // retraction unload (tip forming pauses are <1 s)
constexpr uint32_t kFreeTopUpBlockMs = 60000; // after an unload releases the gear, never push the
                                              // filament back towards the printer for one minute
constexpr uint32_t kK1BootWaitMs = 8000;      // hold the first top-up until the printer answers
constexpr float kUnloadExtraMm = 15.0f;   // keep pulling after the tip passes IN2
constexpr float kUnloadMarginMm = 300.0f;   // only used until the real length has been measured
constexpr float kUnloadLearnMargin = 1.3f;  // allowance on top of the measured unload length
constexpr float kPtestMinExtrudeMm = 20.0f;  // the printer consumes at least this much per level
constexpr int kPtestFeedrate = 300;        // mm/min for the test extrusion
constexpr uint32_t kPtestFillMs = 20000;   // waiting for the buffer to fill before a level starts
constexpr uint32_t kPtestWatchMs = 45000;
constexpr uint32_t kPtestHeatMs = 420000;  // giving up on the nozzle reaching temperature
// Standstill currents the hold part tries (mA): fine steps where the answer usually is, coarser
// above. Steps above the run current are skipped - the driver cannot hold harder than it runs.
constexpr uint16_t kHoldLadder[] = {60,  90,  120, 150, 180, 200, 250, 300,
                                    350, 400, 450, 500, 550, 600, 650, 700};
constexpr int kHoldLadderLen = sizeof(kHoldLadder) / sizeof(kHoldLadder[0]);
constexpr int kPtestPushMarginSteps = 1;     // levels added to the lowest push that works
constexpr int kPtestHoldMarginSteps = 2;     // ladder steps added to the lowest hold that works: the
                                             // test only covers a steady pull of about a minute
constexpr float kPtestHoldTolMm = 3.0f;      // hold part: fed may differ from consumed by this
constexpr float kPtestHoldTolShare = 0.05f;  // ... or by this share of it, whichever is larger
constexpr float kPtestTailStepMm = 2.0f;     // small pulls that bring the lever home at the end
constexpr float kPtestTailMarginMm = 10.0f;  // pulled parking + this more and it never came home
constexpr float kPtestDrainSpeed = 10.0f;  // pulling the buffer empty before a push test
constexpr float kPtestDrainMarginMm = 6.0f;   // headroom on top of the parking amount
constexpr float kPtestRefillMarginMm = 10.0f; // a refill longer than parking + this = slipping
constexpr uint32_t kPtestRefillMs = 20000;
constexpr float kPtestTempTolerance = 5.0f;
constexpr int kBtestRuns = 2;               // buffer test: measuring pushes, the shorter one counts
constexpr float kBtestSpeed = 10.0f;        // mm/s while pushing the carriage out
constexpr float kBtestFirstDrainMm = 30.0f; // most the carriage can hold; more = pulling on the K1
constexpr float kBtestMaxHomeMm = 30.0f;    // the carriage has to leave its rest stop within this
constexpr float kBtestMaxTravelMm = 45.0f;  // no end of travel within this = comb sensor dead
constexpr float kBtestHeadroomMm = 5.0f;    // kept free at the outer end for the printer's
                                            // retractions (slicer ~1 mm, PAUSE 3 mm)
constexpr float kBtestSpreadMm = 3.0f;      // runs further apart than this = not trustworthy
constexpr uint8_t kBtestMinOverfeed = 3;
constexpr uint8_t kBtestMaxOverfeed = 25;   // same cap as settings.cpp
constexpr uint32_t kBufSummaryMs = 10000;  // one buffer summary line at most this often
constexpr uint32_t kBufQuietMs = 3000;     // ... or once the top-ups stop for this long
constexpr uint32_t kReleaseDelayMs = 300;
constexpr uint32_t kUiPeriodMs = 200;
constexpr uint32_t kArrowPeriodMs = 40;  // feed arrow frames between full ones (a top-up is ~0.5 s)
constexpr uint32_t kArrowFullMs = 300;   // the arrow stays full this long after a top-up
constexpr uint32_t kVerbosePeriodMs = 250;

State state = State::kEmpty;
uint32_t state_since = 0;
String message;
io::Sensors prev{};
bool verbose = false;
bool tail_passed = false;  // spool ran out, tail is between the inlet and the gear
bool jogging = false;
bool web_hold = false;         // jog held from the web page, kept alive by repeated requests
uint32_t web_hold_deadline = 0;
bool web_hold_refused = false;  // until the button is released

// physical key state
bool swallow_up = false;       // ignore the rest of a press that stopped something
bool swallow_back = false;
bool back_click_pending = false;
uint32_t back_click_ms = 0;
bool unload_from_click = false;  // current unload was started by a single BACK click
uint32_t unload_click_ms = 0;
bool back_convert_pending = false;
bool up_click_pending = false;   // READY: waiting to see whether a second UP click follows
uint32_t up_click_ms = 0;
uint32_t last_active_ms = 0;     // last time the buffer was feeding
uint32_t last_wake_ms = 0;       // last key press or web action
uint32_t last_motion_ms = 0;
uint32_t last_ui_ms = 0;
uint32_t last_arrow_ms = 0;
uint32_t arrow_full_until_ms = 0;  // 0 = no top-up has finished yet
uint32_t last_verbose_ms = 0;

// buffer feeding episode
float episode_start_pos = 0;
uint32_t episode_start_ms = 0;
bool overfeeding = false;
float overfeed_start_pos = 0;
uint32_t overfeed_edges = 0;    // comb edge count when the carriage last moved
float overfeed_edge_pos = 0;
bool buffer_stalled = false;    // last top-up ended because the carriage stopped moving
bool boosted = false;
String feed_prev_msg;  // READY's message before the top-up: the screen keeps showing it and it
                       // comes back afterwards, so a top-up does not make the text flicker

// The last finished top-up, for /api/status: lets the carriage travel be measured remotely.
struct TopUp {
  float home_mm = 0;      // fed before the carriage left its rest stop
  float over_mm = 0;      // fed after that (the overfeed actually used)
  float last_edge_mm = 0; // overfeed at the last comb edge = where the carriage really stopped
  uint32_t edges = 0;     // comb edges during the whole top-up
  bool stalled = false;
} last_topup;
uint32_t topup_seq = 0;   // counts finished top-ups, so a test can wait for the next one

// loading
bool load_soft_phase = false;
float load_start_pos = 0;
bool load_ext_armed = false;    // stop when the K1 toolhead sensor sees the filament
bool load_ext_prev = false;     // last toolhead sensor reading, for edge detection
bool load_ext_baseline = false; // a reading has been taken since the load started
bool load_reached_ext = false;
bool load_full = false;        // a real load, not a short distance nudge
bool load_expect_edge = false; // the printer should see the filament arrive during this load

// letting the printer pull the filament on its own (tip forming / unload)
State free_return = State::kReady;
uint32_t free_until_ms = 0;
bool free_from_k1 = false;   // the printer asked for it, so the printer also ends it
bool free_from_retract = false;  // no marker, worked out from a long retraction: ends when the
                                 // printer stops pulling rather than when a marker clears
uint32_t free_quiet_ms = 0;  // when the printer last stopped retracting
bool free_blocked = true;    // gripped by hand or timed out: stay gripped until the printer
                             // stops asking, otherwise it would reopen on the very next tick.
                             // Starts blocked because an "M117 PBF:FREE" from a G-code that
                             // died halfway stays on the display for ever: it has to clear
                             // once before the feeder acts on it.
bool grip_after_free = false;  // hold the filament until the next state change, so BACK can pull
uint32_t topup_block_until_ms = 0;  // unload guard started by automatic FREE

// standby: READY lets go of the filament while the printer is idle and cold
uint32_t standby_busy_ms = 0;   // last time something kept READY from standing by
bool standby_counting = false;  // nothing does right now, the countdown is running

// following K1 E motion (automatic mode is retract-only; PBF:FOLLOW is bidirectional)
State follow_return = State::kReady;
float follow_start_pos = 0;
float follow_recent_mm = 0;   // followed in the last few retractions, to spot an unload
uint32_t follow_last_ms = 0;
bool buffer_blocked = false;    // logged once while the printer has no filament to push against

// unloading
float load_target_mm = 0;
float unload_start_pos = 0;
float unload_limit_mm = 0;
bool unload_past_gear = false;
float unload_gear_pos = 0;
bool unload_full = false;   // "unload until empty", so reaching the limit means something jammed
bool unload_from_loaded = false;  // started with the filament all the way at the printer

// Pressure test, in two parts that measure two different things (see ptestTick):
//   PUSH: the run current needed to shove filament into the buffer. Costs no filament - the
//         feeder empties the buffer itself and refills it.
//   HOLD: the standstill current needed for the gear to hold the filament back, so the printer
//         eats the buffer instead of pulling fresh filament through the gear. Needs the printer.
enum class PtestPart : uint8_t { kPush, kHold };
enum class PtestPhase : uint8_t { kOff, kHeat, kDrain, kRefill, kFill, kWatch, kTail };
// coarse: every other step.  fine: the step just below the first pass.  confirm: the winner is
// repeated once, so a single lucky run cannot decide the setting.
enum class PtestStage : uint8_t { kCoarse, kFine, kConfirm };
PtestPhase ptest_phase = PtestPhase::kOff;
PtestPart ptest_part = PtestPart::kPush;
PtestStage ptest_stage = PtestStage::kCoarse;
uint16_t ptest_value = 1;       // pressure level (push part) or standstill mA (hold part)
uint16_t ptest_last_fail = 0;   // highest value that failed, 0 = none yet
uint16_t ptest_coarse_pass = 0; // first value that passed the coarse sweep
uint8_t ptest_push_level = 0;  // result of the push part, used as the run current afterwards
float ptest_start_pos = 0;
float ptest_drain_pos = 0;
uint32_t ptest_phase_ms = 0;
float ptest_old_target = 0;  // nozzle target before the test, restored afterwards
float ptest_e0 = 0;          // printer extruder position when the test move was sent
float ptest_consumed = 0;    // what it actually consumed, measured
uint32_t ptest_gcode_done_ms = 0;  // when the test move finished, 0 = still running
float ptest_commanded = 0;   // what the hold part asked the printer to consume
float ptest_tail_mm = 0;     // of that, the small pulls at the end
bool ptest_tail_home = false;  // the lever has come home since the main pull ended
uint32_t ptest_seq = 0;      // topup_seq when the hold part started waiting for a top-up
String ptest_msg;

// Buffer test: pushes the carriage out to the end of its travel (kBtestRuns times) to measure how
// much filament the buffer can store, then sets overfeed_mm with room left for the printer's
// retractions. Costs no filament and needs no heat, only the K1 holding its extruder.
//   DRAIN: pull the stored slack back until the carriage is home
//   PUSH: feed until the comb sensor goes quiet = end of travel
//   PARK_DRAIN / PARK_REFILL: empty it again and let the normal top-up loop park it with the new
//   amount, which also shows whether the saved pressure level can push that far.
enum class BtestPhase : uint8_t { kOff, kDrain, kPush, kParkDrain, kParkRefill };
BtestPhase btest_phase = BtestPhase::kOff;
uint8_t btest_run = 0;
float btest_travel[kBtestRuns] = {};
float btest_home = 0;         // fed before the carriage left its rest stop (last run)
float btest_drain_pos = 0;
float btest_drain_limit = 0;
float btest_push_pos = 0;     // where the current push started
bool btest_released = false;  // the carriage has left its rest stop during this push
float btest_release_pos = 0;
uint32_t btest_edges = 0;     // comb edge count when the carriage last moved
uint32_t btest_edges0 = 0;
float btest_edge_pos = 0;
uint32_t btest_phase_ms = 0;
uint32_t btest_topup_seq = 0;
String btest_msg;

// Full test (first install): the buffer test, then the pressure test with the new parking amount.
bool full_test = false;

// Outcome of the last test, for the web page.
struct TestResult {
  uint32_t seq = 0;       // bumps whenever a test finishes, so the page can tell a new one
  const char *kind = "";  // "pressure" | "buffer" | "full"
  bool ok = false;
  const char *code = "";  // why it failed or what to look at, a key the web page translates
  String detail;          // same text as the log line
  float travel_mm = 0;    // buffer part, 0 = not measured
  float home_mm = 0;
  uint8_t overfeed_old = 0;
  uint8_t overfeed = 0;
  uint8_t pressure = 0;   // pressure part, 0 = not measured
  uint16_t hold_ma = 0;
  bool idle_release_fixed = false;
} test_result;

// path calibration: a load that starts with the filament staged at the gear measures the real
// distance from the gear to the printer's toolhead sensor.
bool load_from_staged = false;

// routine buffer top-ups are collapsed into one summary line
uint16_t buf_cycles = 0;
float buf_cycle_mm = 0;
uint32_t buf_window_ms = 0;
uint32_t buf_last_cycle_ms = 0;

const Settings &cfg() { return settings::get(); }
// The printer's name as the user typed it ("Printer" by default), for everything the user reads.
const char *pn() { return cfg().printer_name.c_str(); }

uint32_t topUpBlockRemainingMs(uint32_t now) {
  if (!topup_block_until_ms) return 0;
  int32_t left = (int32_t)(topup_block_until_ms - now);
  if (left > 0) return (uint32_t)left;
  topup_block_until_ms = 0;
  return 0;
}

// The carriage is a spring-loaded storage arm: pushed out = buffer full (it holds filament),
// at its rest position = buffer empty, so the feeder has to top it up.
bool bufferFull(const io::Sensors &s) { return cfg().full_when_displaced ? !s.buf_rest : s.buf_rest; }
bool bufferEmpty(const io::Sensors &s) { return !bufferFull(s); }
float pos() { return motor::positionMm(); }
bool ptestActive() { return ptest_phase != PtestPhase::kOff; }
bool btestActive() { return btest_phase != BtestPhase::kOff; }
bool testActive() { return ptestActive() || btestActive(); }
// The buffer test drives the motor itself, so the normal top-up loop has to keep out of the way -
// except at the end, where it is used to park the buffer.
bool btestOwnsMotor() { return btestActive() && btest_phase != BtestPhase::kParkRefill; }

String testMsg() {
  if (!testActive()) return message;
  String m = btestActive() ? btest_msg : ptest_msg;
  if (full_test) m = (btestActive() ? "1/2 " : "2/2 ") + m;
  return m;
}

// Loads, unloads and manual moves drag filament through the whole path; a buffer top-up only
// pushes the carriage 10 mm. Giving them their own level lets the buffer run at the low current
// the pressure test measured without the feeder slipping where torque actually matters.
void applyMovePressure() {
  const Settings &c = cfg();
  uint8_t level = c.move_level ? c.move_level : c.pressure_level;
  motor::setCurrentMa(hw::pressureLevelToMa(level), c.hold_ma);
}

// While the pressure test runs, the value under test wins over the saved one.
void applyPressure() {
  uint8_t level = cfg().pressure_level;
  uint16_t hold = cfg().hold_ma;
  if (ptestActive()) {
    bool push = ptest_part == PtestPart::kPush;
    level = push ? (uint8_t)ptest_value : ptest_push_level;
    // The push part wants full holding torque so only the run current is under test.
    hold = push ? hw::pressureLevelToMa(level) : ptest_value;
  }
  motor::setCurrentMa(hw::pressureLevelToMa(level), hold);
}

using logbuf::logf;

// READY <-> FEEDING happens every second or so while the printer runs: logging both ends of
// every top-up would fill the whole log ring with noise, so they are counted instead (see
// flushBufferSummary). Everything else is logged as it happens.
bool routineTopUp(State from, State to) {
  return (from == State::kReady && to == State::kFeeding) ||
         (from == State::kFeeding && to == State::kReady);
}

void enter(State s, const String &msg = "") {
  if ((s != state || msg != message) && !(routineTopUp(state, s) && !verbose)) {
    logf("%s -> %s%s%s", stateName(state), stateName(s), msg.length() ? ": " : "", msg.c_str());
  }
  state = s;
  state_since = millis();
  message = msg;
  grip_after_free = false;  // exitFree sets it again after its own enter()
}

// Picks a resting state from the sensors (boot, alarm cleared, driver back).
void settle() {
  const io::Sensors &s = io::sensors();
  if (s.in2) enter(State::kReady);
  else if (s.in1) enter(State::kInsertWait);
  else enter(State::kEmpty);
}

void enterAlarm(State s, const String &msg) {
  motor::release();
  jogging = false;
  io::alarm(true);
  enter(s, msg);
}

// Pushing only fills the buffer when the far end is anchored. Once the printer reports its
// extruder is empty (after an unload) there is nothing to push against, so a top-up would just
// shove filament down the tube until the stuck alarm fires. Only trust the sensor on a machine
// that has actually reported filament at some point, so printers without one still work.
const char *anchor_reason = "";
bool anchor_waiting = false;

bool printerCanAnchor() {
  moonraker::Snapshot k1 = moonraker::snapshot();
  // Straight after boot the link is not up yet, so we cannot tell whether the printer still holds
  // the filament. Wait for the first answer rather than pushing blindly.
  if (!k1.fresh) {
    bool waiting = cfg().k1_enabled && !cfg().printer_host.isEmpty() && millis() < kK1BootWaitMs;
    anchor_reason = "has not answered yet";
    anchor_waiting = true;
    return !waiting;
  }
  if (!k1.ext_sensor_present) return true;
  if (k1.print_state == moonraker::PrintState::kPrinting) return true;
  anchor_reason = "reports its extruder empty (unloaded)";
  anchor_waiting = false;
  return k1.ext_sensor;
}

// K1 is printing and the user asked for the keys to be locked meanwhile. A lost link keeps the
// lock: the print goes on without Wi-Fi, so only a fresh "not printing" from the printer opens
// it again (or turning the lock or the K1 link off, or restarting the feeder).
bool locked() {
  if (!cfg().lock_printing || !cfg().k1_enabled) return false;
  return moonraker::snapshot().print_state == moonraker::PrintState::kPrinting;
}

// Why the keys are locked: the OLED text, and the reason for the log.
String lockText() {
  return String("LOCKED: ") + pn() + (moonraker::snapshot().fresh ? " printing" : " offline");
}
String lockReason() {
  return String(pn()) + (moonraker::snapshot().fresh ? " is printing" : " link lost during a print");
}

// Say once why the keys stay locked with the printer gone, and once when that ends.
void logLockHeld() {
  static bool held = false;
  bool now_held = locked() && !moonraker::snapshot().fresh;
  if (now_held == held) return;
  held = now_held;
  if (held) logf("%s link lost during a print: keys stay locked until the printer answers", pn());
  else if (locked()) logf("keys still locked: %s is printing", pn());
  else logf("keys unlocked");
}

// The tail left the drive gear: nothing to push any more, the printer handles the runout.
void enterRunout(const char *msg) {
  motor::release();
  enter(State::kTailOut, msg);
  if (tail_passed && cfg().notify_runout) {
    String body = String("料尾已離開喂竹器，") + pn() + " 將執行斷料流程";
    moonraker::notify("PBF: 耗材用完", body.c_str());
  }
}

// ---- Actions ----

void startInsert() {
  tail_passed = false;
  applyMovePressure();
  motor::run(Dir::kFeed, kInsertSpeed);
  enter(State::kInserting, "grabbing filament");
  io::beep(1);
}

void startPreload() {
  tail_passed = false;
  if (cfg().preload_mm == 0) {
    motor::halt();
    enter(State::kStaged, "press UP to load");
    io::beep(1);
    return;
  }
  applyMovePressure();
  motor::moveMm(Dir::kFeed, cfg().preload_mm, kPreloadSpeed);
  enter(State::kPreload, "feeding " + String(cfg().preload_mm) + "mm");
}

// Feeds `mm` towards the printer, the last kSoftLoadMm slowly.
void startLoad(float mm, bool full) {
  applyMovePressure();
  load_full = full;
  load_start_pos = pos();
  load_target_mm = mm;
  moonraker::Snapshot k1 = moonraker::snapshot();
  load_ext_armed = cfg().stop_at_extruder;
  load_ext_baseline = k1.fresh;
  load_ext_prev = k1.ext_sensor;
  // Only expect the filament to show up at the printer when it is not there already and the
  // printer can actually tell us.
  load_expect_edge = cfg().stop_at_extruder && k1.fresh && k1.ext_sensor_present &&
                     !k1.ext_sensor;
  load_reached_ext = false;
  load_from_staged = state == State::kStaged;
  if (load_ext_armed) {
    if (!k1.fresh) logf("load: %s not reachable, will not stop at the extruder", pn());
    else if (!k1.ext_sensor_present) logf("load: no filament sensor in use, feeding the full distance");
    else if (k1.ext_sensor)
      logf("load: the %s filament sensor ALREADY sees filament - no stop until it clears first", pn());
    else logf("load: will stop when the %s filament sensor sees the filament", pn());
  }
  float fast = mm - kSoftLoadMm;
  load_soft_phase = fast <= 0;
  if (load_soft_phase) motor::moveMm(Dir::kFeed, mm, kSoftLoadSpeed);
  else motor::moveMm(Dir::kFeed, fast, cfg().load_speed);
  enter(State::kLoading, "LOAD 0/" + String(mm, 0) + "mm");
}

// Pulls the filament back until the tip passes the drive gear, or for at most `limit_mm`.
void startUnload(float limit_mm, bool full) {
  applyMovePressure();
  unload_full = full;
  // Only a full unload that starts at the printer measures the whole path.
  moonraker::Snapshot k1 = moonraker::snapshot();
  unload_from_loaded = full && (state == State::kReady || state == State::kFeeding) &&
                       (!k1.fresh || !k1.ext_sensor_present || k1.ext_sensor);
  unload_start_pos = pos();
  unload_limit_mm = limit_mm;
  unload_past_gear = false;
  motor::run(Dir::kRetract, cfg().unload_speed);
  enter(State::kUnloading, "pulling back");
}

// How far a full unload is allowed to pull before it counts as jammed. Every setup has a
// different path length, so the feeder measures its own: the last unload that actually got the
// tip out of the gear sets the yardstick, and only until then is the preload + load estimate used.
float fullUnloadLimit() {
  if (cfg().lock_move_distance) return cfg().unload_mm;
  if (cfg().unload_measured_mm) {
    return cfg().unload_measured_mm * kUnloadLearnMargin + kUnloadExtraMm;
  }
  // A load that stops at the sensor goes extruder_extra_mm past it, and the calibrated load
  // length only reaches the sensor.
  float extra = cfg().stop_at_extruder ? cfg().extruder_extra_mm : 0;
  return cfg().preload_mm + cfg().load_mm + extra + kUnloadMarginMm;
}

void startFeedingEpisode() {
  if (state == State::kReady) feed_prev_msg = message;
  episode_start_pos = pos();
  episode_start_ms = millis();
  overfeeding = false;
  boosted = false;
  buffer_stalled = false;
  io::resetBufPosEdges();
  applyPressure();
  motor::run(Dir::kFeed, cfg().buffer_speed);
  enter(State::kFeeding, "buffer empty: topping up");
}

// ---- Pressure test ----
// Two parts, because "enough current" means two different things. The run current has to push
// filament into the buffer against the carriage spring; the standstill current has to hold the
// filament back so the printer empties the buffer instead of pulling fresh filament through the
// gear. Only the second one burns power all print long, so they are measured separately and each
// gets its own step of margin.
//
// PUSH part (no filament used): ask the printer to hold its extruder, retract just the slack the
// buffer parked with, then let the normal top-up loop refill it. If the motor is too weak the
// carriage never moves (the comb sensor goes quiet) or the refill runs away into a stuck alarm.
//
// HOLD part (needs the printer): the feeder only ever tops up in whole cycles (~ parking amount +
// the slack before the lever leaves home), so a fixed pull of a few cycles cannot be judged by
// the ratio alone - it lands on 2 or 3 top-ups depending on where the lever happened to start.
// So each step starts and ends in the same state, right after a top-up:
//   empty the buffer and let it refill -> the printer pulls ptestExtrudeMm() -> more small pulls
//   until the lever comes home once more -> wait for that top-up to finish.
// Then everything the printer took should have come from the feeder: fed == consumed within
// kPtestHoldTolMm / kPtestHoldTolShare. Short = the printer pulled filament through the gear;
// long = the carriage spring pushed filament back past it.

bool ptestPush() { return ptest_part == PtestPart::kPush; }
int holdIndex(uint16_t ma) {  // the ladder step at or just below ma
  int i = 0;
  while (i + 1 < kHoldLadderLen && kHoldLadder[i + 1] <= ma) i++;
  return i;
}
uint16_t ptestMin() { return ptestPush() ? hw::kMinPressureLevel : kHoldLadder[0]; }
uint16_t ptestMax() {
  if (ptestPush()) return hw::kMaxPressureLevel;
  return kHoldLadder[holdIndex(hw::pressureLevelToMa(ptest_push_level))];
}
// n steps up (pressure levels, or ladder steps for the hold part), never past the maximum.
uint16_t ptestUp(uint16_t v, int n) {
  if (ptestPush()) return min<uint16_t>(v + n, ptestMax());
  return min<uint16_t>(kHoldLadder[min(holdIndex(v) + n, kHoldLadderLen - 1)], ptestMax());
}
uint16_t ptestDown(uint16_t v) {
  if (ptestPush()) return v > ptestMin() ? v - 1 : v;
  int i = holdIndex(v);
  return kHoldLadder[i > 0 ? i - 1 : 0];
}

String ptestWhat() { return ptestPush() ? "push" : "hold"; }
String ptestValueName(uint16_t v) {
  return ptestPush() ? "L" + String(v) : String(v) + "mA";
}

// One test move should cover a few buffer cycles, and a cycle is roughly the parking amount plus
// the travel back to the rest switch.
float ptestExtrudeMm() { return max(kPtestMinExtrudeMm, 2.5f * (cfg().overfeed_mm + 4.0f)); }
float ptestRefillLimitMm() { return cfg().overfeed_mm + kPtestRefillMarginMm; }

// ---- Test results, shared by the three tests ----

void testResultBegin(const char *kind) {
  uint32_t seq = test_result.seq;
  test_result = TestResult();
  test_result.seq = seq;
  test_result.kind = kind;
}

void testResultFinish(bool ok, const char *code, const String &detail) {
  test_result.ok = ok;
  test_result.code = code;
  test_result.detail = detail;
  test_result.seq++;
}

// The full test ends here, whichever part finished it.
void fullTestFinish(bool ok, const char *code, const String &detail) {
  full_test = false;
  String summary = detail;
  if (ok) {
    summary = "all OK: carriage travel " + String(test_result.travel_mm, 1) + " mm, parking " +
              String(test_result.overfeed) + " mm, run L" + String(test_result.pressure) + " (" +
              String(hw::pressureLevelToMa(test_result.pressure)) + " mA), hold " +
              String(test_result.hold_ma) + " mA";
    message = "OK L" + String(test_result.pressure) + " " + String(test_result.hold_ma) +
              "mA park " + String(test_result.overfeed) + "mm";
  }
  logf("full test: %s", summary.c_str());
  testResultFinish(ok, code, summary);
}

void ptestEnd(const String &msg, bool ok, const char *code) {
  bool was_active = ptestActive();
  if (ptest_phase != PtestPhase::kOff) {
    if (moonraker::snapshot().nozzle_target != ptest_old_target) {
      char script[32];
      snprintf(script, sizeof(script), "M104 S%d", (int)ptest_old_target);
      moonraker::sendGcode(script);  // put the nozzle back where the test found it
      logf("pressure test: nozzle target back to %d C", (int)ptest_old_target);
    }
    moonraker::sendGcode("SET_STEPPER_ENABLE STEPPER=extruder ENABLE=0");
  }
  ptest_phase = PtestPhase::kOff;
  ptest_msg = "";
  motor::stop();
  // Cut short mid top-up: go back to READY so the normal loop starts a fresh one if needed.
  if (state == State::kFeeding) enter(State::kReady);
  applyPressure();
  message = msg;
  logf("pressure test: %s", msg.c_str());
  if (ok) io::beep(3);
  else io::beep(1, 600, 0);
  if (!was_active) return;
  if (full_test) fullTestFinish(ok, code, ok ? msg : "pressure test: " + msg);
  else testResultFinish(ok, code, msg);
}

void ptestStartValue(uint16_t value) {
  ptest_value = value;
  ptest_phase = PtestPhase::kDrain;
  ptest_phase_ms = millis();
  applyPressure();
  const char *tag = ptest_stage == PtestStage::kFine      ? "fine "
                    : ptest_stage == PtestStage::kConfirm ? "recheck "
                                                          : "";
  ptest_msg = String(tag) + ptestWhat() + " " + ptestValueName(value);
  // Both parts start from the same known state: the buffer emptied by the feeder itself. The push
  // part scores the refill; the hold part lets the printer pull right after it.
  ptest_drain_pos = pos();
  motor::run(Dir::kRetract, kPtestDrainSpeed);
}

void ptestStartHoldPart();

// Saves the lowest value that worked, plus the margin for that part.
void ptestFinishPart(uint16_t lowest) {
  int margin = ptestPush() ? kPtestPushMarginSteps : kPtestHoldMarginSteps;
  uint16_t value = ptestUp(lowest, margin);
  logf("pressure test: lowest %s that works is %s, using %s (%d step%s of margin)",
       ptestWhat().c_str(), ptestValueName(lowest).c_str(), ptestValueName(value).c_str(), margin,
       margin == 1 ? "" : "s");
  if (ptestPush()) {
    ptest_push_level = (uint8_t)value;
    settings::get().pressure_level = (uint8_t)value;
    settings::save();
    test_result.pressure = (uint8_t)value;
    ptestStartHoldPart();
    return;
  }
  settings::get().hold_ma = value;
  settings::save();
  test_result.hold_ma = value;
  ptestEnd("L" + String(cfg().pressure_level) + " / hold " + String(value) + "mA", true, "");
}

void ptestStartConfirm(uint16_t value) {
  logf("pressure test: %s %s looks like the minimum, repeating it to confirm", ptestWhat().c_str(),
       ptestValueName(value).c_str());
  ptest_stage = PtestStage::kConfirm;
  ptestStartValue(value);
}

void ptestValueDone(bool pass) {
  if (ptest_stage == PtestStage::kConfirm) {
    if (pass) {
      ptestFinishPart(ptest_value);
      return;
    }
    logf("pressure test: %s did not hold up on the repeat, carrying on upwards",
         ptestValueName(ptest_value).c_str());
    ptest_last_fail = ptest_value;
    if (ptest_value >= ptestMax()) {
      ptestEnd("no " + ptestWhat() + " setting worked, check the path", false,
                ptestPush() ? "push_failed" : "hold_failed");
      return;
    }
    ptest_stage = PtestStage::kCoarse;  // close to the boundary now, so take single steps
    ptestStartValue(ptestUp(ptest_value, 1));
    return;
  }

  if (ptest_stage == PtestStage::kFine) {  // the step just below the first coarse pass
    ptestStartConfirm(pass ? ptest_value : ptest_coarse_pass);
    return;
  }

  if (pass) {
    // The coarse sweep skips steps, so the real minimum can be one step below this one.
    if (ptest_value > ptestMin() && ptestDown(ptest_value) > ptest_last_fail) {
      ptest_coarse_pass = ptest_value;
      ptest_stage = PtestStage::kFine;
      logf("pressure test: %s works, now checking %s to pin down the minimum",
           ptestValueName(ptest_value).c_str(),
           ptestValueName(ptestDown(ptest_value)).c_str());
      ptestStartValue(ptestDown(ptest_value));
      return;
    }
    ptestStartConfirm(ptest_value);
    return;
  }
  ptest_last_fail = ptest_value;
  if (ptest_value >= ptestMax()) {
    ptestEnd("no " + ptestWhat() + " setting worked, check the path", false,
                ptestPush() ? "push_failed" : "hold_failed");
    return;
  }
  ptestStartValue(ptestUp(ptest_value, 2));
}

void ptestStartPart() {
  ptest_last_fail = 0;
  ptest_coarse_pass = 0;
  ptest_stage = PtestStage::kCoarse;
  ptestStartValue(ptestMin());
}

void ptestStartHoldPart() {
  ptest_part = PtestPart::kHold;
  moonraker::Snapshot k1 = moonraker::snapshot();
  logf("pressure test: hold part next, standstill current %u, %u, %u ... up to %u mA (run current "
       "L%u); the %s pulls %.0f mm per step, then until the lever comes home",
       kHoldLadder[0], kHoldLadder[1], kHoldLadder[2], ptestMax(), ptest_push_level, pn(),
       ptestExtrudeMm());
  if (!k1.can_extrude || k1.nozzle_temp < cfg().ptest_temp - kPtestTempTolerance) {
    char script[32];
    snprintf(script, sizeof(script), "M104 S%u", cfg().ptest_temp);
    moonraker::sendGcode(script);
    logf("pressure test: heating the nozzle to %u C (now %.0f C)", cfg().ptest_temp, k1.nozzle_temp);
    ptest_value = kHoldLadder[0];
    ptest_phase = PtestPhase::kHeat;
    ptest_phase_ms = millis();
    ptest_msg = "heating " + String((int)k1.nozzle_temp) + "/" + String(cfg().ptest_temp) + "C";
    return;
  }
  ptestStartPart();
}

// ---- Starting and stopping the tests ----

// Why a test cannot start right now, or nullptr. The code is what the web page shows.
const char *testRefusal() {
  const io::Sensors &s = io::sensors();
  if (!motor::driverOk()) return "no_power";
  if (!s.in2) return "no_filament";
  if (motor::isMoving() || !(state == State::kReady || state == State::kStaged)) return "busy";
  moonraker::Snapshot k1 = moonraker::snapshot();
  if (!k1.fresh) return "k1_link";
  if (moonraker::isPrinting()) return "k1_printing";
  // Every test needs the K1 extruder to hold the far end of the filament.
  if (k1.ext_sensor_present && !k1.ext_sensor) return "k1_empty";
  return nullptr;
}

String refusalText(const char *code) {  // short enough for the OLED
  if (!strcmp(code, "no_power")) return "no motor power (24V?)";
  if (!strcmp(code, "no_filament")) return "load filament first";
  if (!strcmp(code, "busy")) return "wait for READY";
  if (!strcmp(code, "k1_link")) return String(pn()) + " not connected";
  if (!strcmp(code, "k1_printing")) return String(pn()) + " is printing";
  if (!strcmp(code, "k1_empty")) return String(pn()) + " extruder empty";
  return code;
}

bool testCanStart(const char *kind) {
  if (state == State::kStuck || state == State::kError) {
    io::alarm(false);
    settle();
  }
  const char *code = testRefusal();
  if (!code) return true;
  message = refusalText(code);
  logf("%s test not started: %s (state %s)", kind, message.c_str(), stateName(state));
  testResultBegin(kind);
  testResultFinish(false, code, message);
  io::beep(1, 600, 0);
  return false;
}

void testStartCommon() {
  if (state != State::kReady) enter(State::kReady);
  // The printer end has to be anchored, otherwise the filament just slides into the extruder
  // instead of pushing the carriage out.
  moonraker::sendGcode("SET_STEPPER_ENABLE STEPPER=extruder ENABLE=1");
}

void ptestBegin() {
  testStartCommon();
  ptest_old_target = moonraker::snapshot().nozzle_target;
  ptest_part = PtestPart::kPush;
  ptest_push_level = cfg().pressure_level;
  logf("pressure test: push part first, sweeping the run current from L%u - no filament is used",
       hw::kMinPressureLevel);
  ptestStartPart();
}

void btestEnd(const String &msg, bool ok, const char *code) {
  if (!btestActive()) return;
  // While parking, the motor belongs to the normal top-up loop: leave it alone.
  if (btestOwnsMotor()) motor::halt();
  btest_phase = BtestPhase::kOff;
  btest_msg = "";
  applyPressure();
  moonraker::sendGcode("SET_STEPPER_ENABLE STEPPER=extruder ENABLE=0");
  message = msg;
  logf("buffer test: %s", msg.c_str());
  if (ok) io::beep(3);
  else io::beep(1, 600, 0);
  if (full_test) fullTestFinish(ok, code, "buffer test: " + msg);
  else testResultFinish(ok, code, msg);
}

void stopTests(const char *why) {
  if (ptestActive()) ptestEnd(why, false, "cancelled");
  if (btestActive()) btestEnd(why, false, "cancelled");
}

void btestStartDrain(BtestPhase phase, float limit) {
  btest_phase = phase;
  btest_phase_ms = millis();
  btest_drain_pos = pos();
  btest_drain_limit = limit;
  applyPressure();
  if (bufferFull(io::sensors())) motor::run(Dir::kRetract, kPtestDrainSpeed);
}

void btestStartPush() {
  btest_phase = BtestPhase::kPush;
  btest_phase_ms = millis();
  btest_push_pos = pos();
  btest_released = false;
  btest_edges0 = io::bufPosEdges();
  // The mechanical end is what is being measured, so push with the strong load/unload current:
  // the everyday pressure level might give up earlier. The pressure test checks that one later.
  applyMovePressure();
  motor::run(Dir::kFeed, kBtestSpeed);
}

void btestBegin() {
  testStartCommon();
  btest_run = 0;
  logf("buffer test: pushing the carriage to the end of its travel %d times - no filament is used",
       kBtestRuns);
  btestStartDrain(BtestPhase::kDrain, kBtestFirstDrainMm);
}

// All runs done: pick the parking amount and save it.
void btestEvaluate() {
  float lo = btest_travel[0], hi = btest_travel[0];
  for (int i = 1; i < kBtestRuns; i++) {
    lo = min(lo, btest_travel[i]);
    hi = max(hi, btest_travel[i]);
  }
  if (hi - lo > kBtestSpreadMm) {
    btestEnd("runs disagree (" + String(lo, 1) + " / " + String(hi, 1) + " mm), check the tube",
             false, "inconsistent");
    return;
  }
  int amount = (int)floorf(lo - kBtestHeadroomMm);
  if (amount < kBtestMinOverfeed) {
    btestEnd("only " + String(lo, 1) + " mm of travel, check the tube", false, "short_travel");
    return;
  }
  amount = min<int>(amount, kBtestMaxOverfeed);
  test_result.travel_mm = lo;
  test_result.home_mm = btest_home;
  test_result.overfeed_old = cfg().overfeed_mm;
  test_result.overfeed = (uint8_t)amount;
  logf("buffer test: the carriage travels %.1f mm after leaving home (%.1f mm of slack before "
       "that); parking amount %u -> %d mm, leaving %.1f mm for retractions",
       lo, btest_home, cfg().overfeed_mm, amount, lo - amount);
  settings::get().overfeed_mm = (uint8_t)amount;
  settings::save();
  // Empty it once more: the pressure test (full test) or the normal top-up loop parks it.
  btestStartDrain(BtestPhase::kParkDrain, lo + kOverfeedStallMm + kPtestDrainMarginMm);
}

void startPressureTest() {
  if (testActive()) {  // pressing it again stops the test
    stopTests("cancelled");
    return;
  }
  if (!testCanStart("pressure")) return;
  full_test = false;
  testResultBegin("pressure");
  ptestBegin();
}

void startBufferTest() {
  if (testActive()) {
    stopTests("cancelled");
    return;
  }
  if (!testCanStart("buffer")) return;
  full_test = false;
  testResultBegin("buffer");
  btestBegin();
}

// First install: measure the buffer, then find the currents for that parking amount.
void startFullTest() {
  if (testActive()) {
    stopTests("cancelled");
    return;
  }
  if (!testCanStart("full")) return;
  full_test = true;
  testResultBegin("full");
  if (cfg().idle_release) {
    settings::get().idle_release = false;
    settings::save();
    test_result.idle_release_fixed = true;
    logf("full test: turned idle release off - the buffer only works while the gear holds");
  }
  logf("full test: 1/2 buffer travel (no filament), 2/2 pressure test (heats the nozzle to %u C)",
       cfg().ptest_temp);
  btestBegin();
}

void btestTick(uint32_t now) {
  if (!btestActive()) return;
  const io::Sensors &s = io::sensors();
  if (!s.in2) {
    btestEnd("stopped: no filament", false, "no_filament");
    return;
  }
  if (moonraker::isPrinting()) {
    btestEnd(String("stopped: ") + pn() + " started printing", false, "k1_printing");
    return;
  }
  if (state == State::kError || state == State::kNoDriver) {
    btestEnd("stopped", false, "stopped");
    return;
  }
  if (state == State::kStuck) {  // only possible while the normal loop parks the buffer
    io::alarm(false);
    settle();
    btestEnd("stuck alarm while parking the buffer", false, "park_weak");
    return;
  }
  String run = "buffer " + String(btest_run + 1) + "/" + String(kBtestRuns) + ": ";
  switch (btest_phase) {
    case BtestPhase::kDrain:
    case BtestPhase::kParkDrain: {
      float pulled = btest_drain_pos - pos();
      btest_msg = (btest_phase == BtestPhase::kDrain ? run : String("parking: ")) + "emptying " +
                  String(pulled, 0) + "mm";
      if (bufferEmpty(s)) {
        motor::halt();
        if (btest_phase == BtestPhase::kDrain) {
          btestStartPush();
        } else if (full_test) {
          btest_phase = BtestPhase::kOff;
          btest_msg = "";
          logf("full test: buffer part done, pressure test next");
          ptestBegin();
        } else {
          btest_phase = BtestPhase::kParkRefill;
          btest_phase_ms = now;
          btest_topup_seq = topup_seq;
          btest_msg = "parking: refilling";
        }
        break;
      }
      // Never keep pulling once the slack should be gone - past that the filament is being
      // dragged back out of the printer's extruder, which only grinds it.
      if (pulled >= btest_drain_limit || !motor::isMoving()) {
        btestEnd("could not empty the buffer in " + String(pulled, 0) + " mm", false,
                 "drain_failed");
      }
      break;
    }

    case BtestPhase::kPush: {
      uint32_t edges = io::bufPosEdges();
      if (!btest_released) {
        btest_msg = run + "leaving home " + String(pos() - btest_push_pos, 0) + "mm";
        if (bufferFull(s)) {
          btest_released = true;
          btest_release_pos = pos();
          btest_edge_pos = pos();
          btest_edges = edges;
        } else if (pos() - btest_push_pos > kBtestMaxHomeMm) {
          btestEnd("carriage never left home in " + String(kBtestMaxHomeMm, 0) +
                       " mm - is the filament in the " + pn() + " extruder?",
                   false, "never_left_home");
        }
        break;
      }
      // Same rule as a normal top-up: the comb sensor going quiet for kOverfeedStallMm means the
      // carriage has stopped. Where it last ticked is how far it really got.
      if (edges != btest_edges) {
        btest_edges = edges;
        btest_edge_pos = pos();
      }
      btest_msg = run + String(pos() - btest_release_pos, 0) + "mm out";
      if (pos() - btest_edge_pos >= kOverfeedStallMm) {
        motor::halt();
        float travel = btest_edge_pos - btest_release_pos;
        btest_travel[btest_run] = travel;
        btest_home = btest_release_pos - btest_push_pos;
        logf("buffer test run %u: left home after %.1f mm, carriage stopped %.1f mm later (%lu "
             "comb edges)",
             btest_run + 1, btest_home, travel, (unsigned long)(edges - btest_edges0));
        btest_run++;
        if (btest_run < kBtestRuns) {
          btestStartDrain(BtestPhase::kDrain, travel + kOverfeedStallMm + kPtestDrainMarginMm);
        } else {
          btestEvaluate();
        }
      } else if (pos() - btest_release_pos > kBtestMaxTravelMm) {
        btestEnd("no end of travel within " + String(kBtestMaxTravelMm, 0) +
                     " mm - comb sensor not working?",
                 false, "no_end");
      }
      break;
    }

    case BtestPhase::kParkRefill: {  // the normal top-up loop parks it; check it got there
      bool done = topup_seq != btest_topup_seq && state == State::kReady && !motor::isMoving();
      bool late = now - btest_phase_ms > kPtestRefillMs;
      if (!done && !late) break;
      String result = "carriage travel " + String(test_result.travel_mm, 1) + " mm, parking " +
                      String(test_result.overfeed) + " mm";
      if (done && !last_topup.stalled && last_topup.over_mm >= cfg().overfeed_mm - 0.5f) {
        btestEnd(result, true, "");
      } else {
        // The measurement stands; only the everyday pressure level is too weak for it.
        btestEnd(result + ", but L" + String(cfg().pressure_level) +
                     " could not push that far - run the pressure test",
                 true, "park_weak");
      }
      break;
    }

    default:
      break;
  }
}

void ptestTick(uint32_t now) {
  if (!ptestActive()) return;
  const io::Sensors &s = io::sensors();
  if (!s.in2) {
    ptestEnd("stopped: no filament", false, "no_filament");
    return;
  }
  if (moonraker::isPrinting()) {
    ptestEnd(String("stopped: ") + pn() + " started printing", false, "k1_printing");
    return;
  }
  if (state == State::kError || state == State::kNoDriver) {
    ptestEnd("stopped", false, "stopped");
    return;
  }
  if (state == State::kStuck) {  // this setting cannot keep the buffer full: that is the failure
    io::alarm(false);
    settle();
    logf("pressure test %s %s: stuck alarm while refilling", ptestWhat().c_str(),
         ptestValueName(ptest_value).c_str());
    ptestValueDone(false);
    return;
  }
  switch (ptest_phase) {
    case PtestPhase::kHeat: {
      moonraker::Snapshot k1 = moonraker::snapshot();
      ptest_msg = "heating " + String((int)k1.nozzle_temp) + "/" + String(cfg().ptest_temp) + "C";
      if (k1.can_extrude && k1.nozzle_temp >= cfg().ptest_temp - kPtestTempTolerance) {
        logf("pressure test: nozzle at %.0f C, starting the hold part", k1.nozzle_temp);
        ptestStartPart();
      } else if (now - ptest_phase_ms > kPtestHeatMs) {
        ptestEnd("nozzle did not reach temperature", false, "heat_failed");
      }
      break;
    }

    case PtestPhase::kDrain: {  // pulling back the slack the buffer parked with
      float pulled = ptest_drain_pos - pos();
      ptest_msg = "push " + ptestValueName(ptest_value) + ": draining " + String(pulled, 0) + "mm";
      if (bufferEmpty(s)) {
        motor::halt();
        ptest_start_pos = pos();
        ptest_seq = topup_seq;
        ptest_phase = ptestPush() ? PtestPhase::kRefill : PtestPhase::kFill;
        ptest_phase_ms = now;
        break;
      }
      // Never keep pulling once the slack is gone - past that the filament is being dragged back
      // out of the printer's extruder, which only grinds it.
      if (pulled >= cfg().overfeed_mm + kPtestDrainMarginMm || !motor::isMoving()) {
        motor::halt();
        logf("pressure test %s %s: could not empty the buffer in %.0f mm (motor too weak?)",
             ptestWhat().c_str(), ptestValueName(ptest_value).c_str(), pulled);
        ptestValueDone(false);
      }
      break;
    }

    case PtestPhase::kRefill: {  // the normal top-up loop does the work, we just score it
      float fed = pos() - ptest_start_pos;
      ptest_msg = "push " + ptestValueName(ptest_value) + ": refill " + String(fed, 0) + "mm";
      if (bufferFull(s) && !motor::isMoving() && state == State::kReady) {
        // A partial stall still ends the top-up "successfully", so check for it as well.
        bool ok = fed <= ptestRefillLimitMm() && !buffer_stalled;
        logf("pressure test push %s (%u mA): refilled %.1f mm (limit %.0f), carriage stalled: %s "
             "-> %s",
             ptestValueName(ptest_value).c_str(), hw::pressureLevelToMa((uint8_t)ptest_value), fed,
             ptestRefillLimitMm(), buffer_stalled ? "YES" : "no", ok ? "PASS" : "fail");
        ptestValueDone(ok);
      } else if (now - ptest_phase_ms > kPtestRefillMs) {
        logf("pressure test push %s: buffer did not refill in %lu s", ptestValueName(ptest_value).c_str(),
             (unsigned long)(kPtestRefillMs / 1000));
        ptestValueDone(false);
      }
      break;
    }

    case PtestPhase::kFill:  // hold part: wait for the refill to finish, then pull at once
      ptest_msg = "hold " + ptestValueName(ptest_value) + ": refilling";
      if (topup_seq != ptest_seq && state == State::kReady && bufferFull(s) && !motor::isMoving()) {
        moonraker::Snapshot k1 = moonraker::snapshot();
        ptest_start_pos = pos();
        ptest_e0 = k1.extruder_pos;
        ptest_commanded = ptestExtrudeMm();
        ptest_tail_mm = 0;
        char script[64];
        // M400 matters: without it Moonraker answers as soon as the move is queued.
        snprintf(script, sizeof(script), "M83\nG1 E%.0f F%d\nM400", ptestExtrudeMm(),
                 kPtestFeedrate);
        moonraker::sendGcode(script);
        ptest_phase = PtestPhase::kWatch;
        ptest_phase_ms = now;
      } else if (now - ptest_phase_ms > kPtestFillMs) {
        logf("pressure test hold %s: the buffer did not refill", ptestValueName(ptest_value).c_str());
        ptestValueDone(false);
      }
      break;

    case PtestPhase::kWatch:  // the main pull
      ptest_msg = "hold " + ptestValueName(ptest_value) + ": " + pn() + " pulling, fed " +
                  String(pos() - ptest_start_pos, 0) + "mm";
      if (moonraker::gcodeBusy()) {
        if (now - ptest_phase_ms > kPtestWatchMs) {
          logf("pressure test hold %s: the printer move did not finish",
               ptestValueName(ptest_value).c_str());
          ptestValueDone(false);
        }
        break;
      }
      // End the window the way it started, right after a top-up (see the comment on the hold
      // part): wait for the one under way, or make the printer pull until the lever comes home.
      ptest_phase = PtestPhase::kTail;
      ptest_phase_ms = now;
      ptest_tail_home = false;
      ptest_seq = topup_seq;
      ptest_gcode_done_ms = 0;
      break;

    case PtestPhase::kTail: {
      if (bufferEmpty(s) || state == State::kFeeding) ptest_tail_home = true;
      if (!ptest_tail_home) {
        ptest_msg = "hold " + ptestValueName(ptest_value) + ": bringing the lever home " +
                    String(ptest_tail_mm, 0) + "mm";
        if (moonraker::gcodeBusy()) break;
        if (ptest_tail_mm >= cfg().overfeed_mm + kPtestTailMarginMm) {
          logf("pressure test hold %s: the lever did not come home after %.0f mm more",
               ptestValueName(ptest_value).c_str(), ptest_tail_mm);
          ptestValueDone(false);
          break;
        }
        char script[48];
        snprintf(script, sizeof(script), "M83\nG1 E%.0f F%d\nM400", kPtestTailStepMm,
                 kPtestFeedrate);
        moonraker::sendGcode(script, true);
        ptest_tail_mm += kPtestTailStepMm;
        ptest_commanded += kPtestTailStepMm;
        break;
      }
      ptest_msg = "hold " + ptestValueName(ptest_value) + ": last top-up";
      bool done = topup_seq != ptest_seq && state == State::kReady && !motor::isMoving() &&
                  !moonraker::gcodeBusy();
      if (!done) {
        ptest_gcode_done_ms = 0;
        if (now - ptest_phase_ms > kPtestWatchMs) {
          logf("pressure test hold %s: the last top-up did not finish",
               ptestValueName(ptest_value).c_str());
          ptestValueDone(false);
        }
        break;
      }
      // The poll task waits while an M400 runs, so give it time for a fresh printer position.
      if (!ptest_gcode_done_ms) ptest_gcode_done_ms = now;
      if (now - ptest_gcode_done_ms < 800) break;
      moonraker::Snapshot k1 = moonraker::snapshot();
      float used = k1.fresh ? k1.extruder_pos - ptest_e0 : 0;
      // G92 in a macro, or a stale reading, would make this nonsense: fall back to what we asked
      // the printer to consume.
      ptest_consumed = (used > 1.0f && used < 3.0f * ptest_commanded) ? used : ptest_commanded;
      float fed = pos() - ptest_start_pos;
      float diff = fed - ptest_consumed;
      float tol = max(kPtestHoldTolMm, kPtestHoldTolShare * ptest_consumed);
      bool ok = fabsf(diff) <= tol;
      logf("pressure test hold %u mA standstill: fed %.1f for %.1f mm consumed (%+.1f mm, "
           "allowed %.1f)%s -> %s",
           ptest_value, fed, ptest_consumed, diff, tol,
           ok ? ""
           : diff < 0 ? ", the printer pulled filament through the gear"
                      : ", the spring pushed filament back past the gear",
           ok ? "PASS" : "fail");
      ptestValueDone(ok);
      break;
    }

    default:
      break;
  }
}

// One line per burst of routine top-ups instead of four lines per top-up.
void flushBufferSummary(uint32_t now) {
  if (!buf_cycles) return;
  if (now - buf_window_ms < kBufSummaryMs && now - buf_last_cycle_ms < kBufQuietMs) return;
  logf("buffer: %u top-ups, %.0f mm in %.1f s", (unsigned)buf_cycles, buf_cycle_mm,
       (buf_last_cycle_ms - buf_window_ms) / 1000.0f);
  buf_cycles = 0;
  buf_cycle_mm = 0;
}

// ---- Per-state updates ----

void updateFeeding(const io::Sensors &s, bool in1_fall) {
  if (!s.in2) {
    enterRunout("runout: printer handles it");
    return;
  }
  if (in1_fall) {
    tail_passed = true;
    logf("tail passed the inlet sensor");
  }
  float fed = pos() - episode_start_pos;
  if (bufferFull(s)) {
    // The carriage has left its rest stop; keep pushing a little so the buffer parks with some
    // slack in it. Parking right at the rest stop means the printer has to break the carriage
    // away from it on every single pull, which is where most of its effort goes.
    if (!overfeeding) {
      overfeeding = true;
      overfeed_start_pos = pos();
      overfeed_edges = io::bufPosEdges();
      overfeed_edge_pos = pos();
    } else {
      // The comb sensor ticks while the carriage travels. If it goes quiet while we keep pushing,
      // the carriage has run out of travel and the filament would only buckle: stop, whatever the
      // overfeed setting says.
      uint32_t edges = io::bufPosEdges();
      if (edges != overfeed_edges) {
        overfeed_edges = edges;
        overfeed_edge_pos = pos();
      }
      bool stalled = pos() - overfeed_edge_pos >= kOverfeedStallMm;
      if (stalled || pos() - overfeed_start_pos >= cfg().overfeed_mm) {
        motor::stop();
        last_topup.home_mm = overfeed_start_pos - episode_start_pos;
        last_topup.over_mm = pos() - overfeed_start_pos;
        last_topup.last_edge_mm = overfeed_edge_pos - overfeed_start_pos;
        last_topup.edges = io::bufPosEdges();
        last_topup.stalled = stalled;
        topup_seq++;
        if (stalled) {
          buffer_stalled = true;
          logf("buffer: carriage stopped moving %.1f mm into the overfeed (last comb edge at "
               "%.1f mm, left home after %.1f mm, %lu edges), parking here",
               last_topup.over_mm, last_topup.last_edge_mm, last_topup.home_mm,
               (unsigned long)last_topup.edges);
        }
        if (verbose) {
          logf("buffer full: fed %.1f mm in %lu ms, comb edges %lu", fed,
               (unsigned long)(millis() - episode_start_ms), (unsigned long)io::bufPosEdges());
        }
        if (!buf_cycles) buf_window_ms = episode_start_ms;
        buf_cycles++;
        stats::addTopUps(1, fed);
        buf_cycle_mm += fed;
        buf_last_cycle_ms = millis();
        arrow_full_until_ms = millis() + kArrowFullMs;
        enter(State::kReady, feed_prev_msg);
        return;
      }
    }
  } else {
    overfeeding = false;
  }
  if (!boosted && millis() - episode_start_ms > kBoostAfterMs) {
    boosted = true;
    motor::setSpeed(cfg().buffer_speed * kBoostFactor);
  }
  if (!tail_passed && bufferEmpty(s) && fed > cfg().stuck_mm) {
    stats::addStuck();
    enterAlarm(State::kStuck, "fed " + String(fed, 0) + "mm, buffer still empty");
    if (cfg().pause_on_stuck && !testActive()) {
      if (moonraker::isPrinting()) moonraker::sendGcode("PAUSE");
      moonraker::notify("PBF: 卡料", "輔助進料後緩衝沒有回來，請檢查線盤或路徑");
    }
    return;
  }
  if (!motor::isMoving()) {
    enter(State::kReady, feed_prev_msg);
    return;
  }
  message = "fed " + String(fed, 1) + "mm";
}

void updateUnloading(const io::Sensors &s) {
  float moved = unload_start_pos - pos();
  if (!unload_past_gear) {
    if (!s.in2) {
      unload_past_gear = true;
      unload_gear_pos = pos();
      if (!cfg().lock_move_distance && unload_from_loaded && moved > 10) {
        // With distance locking off, a successful full unload teaches the next unload its limit.
        uint16_t measured = (uint16_t)min(moved, (float)settings::kMaxMoveMm);
        if (measured != cfg().unload_measured_mm) {
          logf("unload: the tip cleared the gear after %.0f mm, using that as the yardstick "
               "(limit %.0f mm)",
               moved, measured * kUnloadLearnMargin + kUnloadExtraMm);
          settings::get().unload_measured_mm = measured;
          settings::save();
        }
      }
    }
  } else if (unload_gear_pos - pos() >= kUnloadExtraMm) {
    motor::stop();
    enter(State::kUnloaded, "pull out or reload");
    io::beep(2);
    return;
  }
  if (moved >= unload_limit_mm) {
    motor::stop();
    if (s.in2 && unload_full) {
      // Asked to unload completely and the tip never reached the gear: it is jammed, usually the
      // shaped tip catching on the way back. Say so instead of stopping quietly - pulling the
      // filament by hand at this point is what gets it out.
      stats::addUnloadJam();
      enterAlarm(State::kError, "tip stuck: " + String(moved, 0) + "mm pulled, still in the gear");
      moonraker::notify("PBF: 退料卡住", "料尾沒有退出齒輪，請手動拉一下耗材");
    } else if (s.in2) {
      // Distance done but the filament is still in the gear: stay loaded, just idle.
      enter(State::kStaged, "retracted " + String(moved, 0) + "mm");
    } else {
      enter(State::kUnloaded, "unload limit reached, check");
    }
    io::beep(2);
    return;
  }
  if (!motor::isMoving()) {
    enter(s.in2 ? State::kStaged : State::kUnloaded, "unload stopped");
    return;
  }
  message = "back " + String(moved, 0) + "mm";
}

// Starts following K1 E motion. Without a marker this remains the old retract-only fallback;
// an explicit PBF:FOLLOW marker mirrors both positive (feed) and negative (retract) motion.
// Defined below with the rest of the free-gear handling; FOLLOW needs it to bail out.
void enterFree(const String &why, bool from_k1, bool from_retract = false);

bool maybeStartFollowing() {
  moonraker::Snapshot k1 = moonraker::snapshot();
  bool explicit_follow = k1.pbf_mode == 2;
  if ((!cfg().retract_sync && !explicit_follow) || motor::isMoving() || jogging || testActive()) {
    return false;
  }
  // Never mirror positive E motion during an actual print. A forgotten M117 marker would
  // otherwise turn every extrusion move into an uncontrolled feeder move.
  if (!k1.fresh || k1.age_ms > kFollowMaxAgeMs ||
      k1.print_state == moonraker::PrintState::kPrinting) {
    return false;
  }
  float velocity = k1.extruder_velocity;
  if (explicit_follow ? fabsf(velocity) < kFollowStartMmS : velocity > -kFollowStartMmS) {
    return false;
  }
  follow_return = state;
  follow_start_pos = pos();
  applyMovePressure();
  Dir dir = explicit_follow && velocity > 0 ? Dir::kFeed : Dir::kRetract;
  motor::run(dir, min(fabsf(velocity), kFollowMaxSpeed));
  enter(State::kFollowing, String("following ") + pn() + (dir == Dir::kFeed ? " feed" : " retract"));
  return true;
}

void updateFollowing(const io::Sensors &s) {
  moonraker::Snapshot k1 = moonraker::snapshot();
  bool explicit_follow = k1.pbf_mode == 2;
  float followed = follow_start_pos - pos();
  if (!s.in2) {
    motor::stop();
    enter(State::kUnloaded, "followed " + String(fabsf(followed), 0) +
                                "mm, filament left the gear");
    return;
  }
  // Fallback for an unload that carries no marker: a print retraction is a few millimetres, so
  // this much means the printer is unloading. Chasing the rest of it is pointless and, once tip
  // forming starts oscillating, actively harmful - let go instead and let the printer pull.
  const Settings &c = cfg();
  if (!explicit_follow && c.free_on_unload && c.free_on_retract &&
      follow_recent_mm + followed >= c.free_retract_mm && !testActive()) {
    logf("retracted %.0f mm without a marker: treating it as an unload",
         follow_recent_mm + followed);
    enterFree("long retract: gear released", false, true);
    return;
  }
  float velocity = k1.extruder_velocity;
  bool e_moving = explicit_follow ? fabsf(velocity) > kFollowStopMmS : velocity < -kFollowStopMmS;
  bool keep = k1.fresh && k1.age_ms <= kFollowMaxAgeMs && e_moving &&
              k1.print_state != moonraker::PrintState::kPrinting;
  if (!keep || !motor::isMoving()) {
    motor::stop();
    const char *motion = followed < 0 ? "feed" : "retract";
    logf("followed %s %s: %.1f mm", pn(), motion, fabsf(followed));
    uint32_t now = millis();
    if (followed > 0) {
      if (now - follow_last_ms > kFollowWindowMs) follow_recent_mm = 0;  // unrelated retraction
      follow_recent_mm += followed;
      follow_last_ms = now;
    } else if (explicit_follow) {
      follow_recent_mm = 0;  // a marked load is not part of a preceding unload
    }
    // A print retraction is a few millimetres. This much means the printer is unloading, and
    // topping the buffer up now would push the filament straight back into the extruder.
    if (follow_recent_mm >= kUnloadFollowMm && state != State::kStaged &&
        follow_return != State::kStaged) {
      logf("that is an unload (%.0f mm), not a print retraction: pausing the buffer",
           follow_recent_mm);
      enter(State::kStaged, "unload " + String(follow_recent_mm, 0) + "mm, buffer paused");
      io::beep(1);
      return;
    }
    enter(follow_return, "followed " + String(motion) + " " +
                             String(fabsf(followed), 0) + "mm");
    return;
  }
  Dir wanted = explicit_follow && velocity > 0 ? Dir::kFeed : Dir::kRetract;
  if (motor::direction() != wanted) {
    motor::run(wanted, min(fabsf(velocity), kFollowMaxSpeed));
  } else {
    motor::setSpeed(min(fabsf(velocity), kFollowMaxSpeed));
  }
  message = String(wanted == Dir::kFeed ? "following feed " : "following retract ") +
            String(fabsf(followed), 0) + "mm";
}

// ---- Letting go of the filament while the printer unloads ----
//
// A tip-forming routine oscillates the filament in and out several times. FOLLOW cannot
// track that: it reacts to a 200 ms poll, so it spends the whole routine chasing moves that
// are already over, yanking the buffer carriage about. Releasing the driver instead lets the
// gear free-wheel, and the printer moves the filament on its own.
//
// This only works because the printer's unload stops once the tip is clear of its extruder
// (about 100 mm). A longer pull has to be driven by the motor, or the slack piles up at the
// inlet with nothing to rewind it.
bool canFree() {
  switch (state) {
    case State::kStaged:
    case State::kReady:
    case State::kFeeding:
    case State::kFollowing:
    case State::kUnloaded:
    case State::kTailOut:
      return !jogging && !web_hold && !testActive() && motor::driverOk() && !locked();
    default:
      return false;
  }
}

void enterFree(const String &why, bool from_k1, bool from_retract) {
  if (state == State::kFree) return;
  if (motor::isMoving()) motor::halt();
  jogging = false;
  // Returning into FOLLOW would restart a retraction that is already over.
  free_return = state == State::kFollowing ? follow_return : state;
  free_from_k1 = from_k1;
  free_from_retract = from_retract;
  free_quiet_ms = millis();
  if (from_k1 || from_retract) topup_block_until_ms = millis() + kFreeTopUpBlockMs;
  // The feeder stops measuring the moment the gear free-wheels, so the running total that
  // decided this is no longer about anything real.
  follow_recent_mm = 0;
  free_until_ms = millis() + (uint32_t)cfg().free_timeout_s * 1000UL;
  motor::release();
  enter(State::kFree, why);
  io::beep(1);
}

// block: the printer may still be asking for a free gear, but do not honour it again until it
// stops asking. Without this a key press or the timeout would be undone on the next tick.
void exitFree(const String &why, bool block) {
  free_until_ms = 0;
  free_blocked = block;
  free_from_retract = false;
  // Whatever happens next is a load or an unload of several hundred mm, so grip with the
  // current those use rather than the one the buffer tops up with.
  applyMovePressure();
  motor::hold();
  if (!io::sensors().in2) {
    logf("gear gripped again: %s", why.c_str());
    settle();
    grip_after_free = true;
    return;
  }
  // The printer reporting an empty toolhead means its unload finished: park in STAGED so the
  // buffer stays off until the user loads again (and BACK can pull the filament back).
  moonraker::Snapshot k1 = moonraker::snapshot();
  bool unloaded = k1.fresh && k1.ext_sensor_present && !k1.ext_sensor;
  enter(unloaded ? State::kStaged : free_return, why);
  // STAGED normally lets the driver go, but the next thing the user does is press BACK to pull
  // the filament home: keep hold of it until something else changes the state.
  grip_after_free = true;
}

void freeTick(uint32_t now) {
  moonraker::Snapshot k1 = moonraker::snapshot();
  bool printing = k1.fresh && k1.print_state == moonraker::PrintState::kPrinting;
  if (printing && topUpBlockRemainingMs(now)) {
    topup_block_until_ms = 0;
    logf("%s started printing: unload guard cancelled, buffer top-ups restored", pn());
  }
  // Stale data must not keep the gear open: a feeder that is not holding is a feeder that does
  // nothing, because the printer then pulls filament straight off the spool through the gear.
  bool asked = cfg().free_on_unload && k1.fresh && (k1.pbf_macro || k1.pbf_display) &&
               k1.pbf_mode == 1 && !printing;

  if (state == State::kFree) {
    // A marker turning up mid-way takes over: it knows exactly when the unload ends.
    if (asked) {
      free_from_k1 = true;
      free_from_retract = false;
    }
    if (k1.fresh && k1.extruder_velocity < -kFollowStopMmS) free_quiet_ms = now;
    String lost = String("lost the ") + pn() + " link";
    if (printing) {
      exitFree(String(pn()) + " started printing", false);
    } else if (free_from_k1 && !asked) {
      exitFree(k1.fresh ? String(pn()) + " stopped asking" : lost, false);
    } else if (!k1.fresh && (free_from_k1 || free_from_retract)) {
      exitFree(lost, false);
    } else if (free_from_retract && now - free_quiet_ms > kFreeQuietMs) {
      exitFree(String(pn()) + " stopped pulling", false);
    } else if ((int32_t)(now - free_until_ms) > 0) {
      exitFree("timed out", true);
    } else {
      message = "gear released, " + String((free_until_ms - now) / 1000) + "s left";
    }
    return;
  }
  if (!asked) free_blocked = false;  // the printer stopped asking: arm it again
  if (asked && !free_blocked && canFree()) enterFree(String(pn()) + " unloading: gear released", true);
}

// ---- Standby ----
//
// READY holds the filament at the standstill current around the clock, because the buffer only
// works while the gear holds. That is only needed while the printer can actually pull. Klipper
// refuses to extrude a cold nozzle, so anything that moves filament - a print, a load or unload
// macro, the printer's own screen - has to heat the nozzle first. A printer that is not printing,
// not heating and too cold to extrude cannot pull, so the gear can let go, and the moment it
// starts heating the feeder grips again, minutes before the first extrusion.

// Why the printer might move the filament soon, or nullptr when it cannot. With the link down
// the last answer still counts: a printer that was switched off stays cold, and one that has
// never answered since boot is most likely off.
// The reason reads after the printer's name: "K1 heating".
const char *k1Busy(const moonraker::Snapshot &k1) {
  if (!cfg().k1_enabled || cfg().printer_host.isEmpty()) return "link off";
  if (k1.print_state == moonraker::PrintState::kPrinting) return "printing";
  if (k1.print_state == moonraker::PrintState::kPaused) return "paused";
  if (k1.nozzle_target > 0) return "heating";
  if (k1.can_extrude) return "nozzle hot";
  if (k1.fresh && fabsf(k1.extruder_velocity) > kFollowStopMmS) return "E moving";
  return nullptr;
}

void exitStandby(const String &why) {
  applyPressure();
  motor::hold();
  enter(State::kReady, String("woke: ") + why);
}

// Called while READY: counts down standby_min of nothing happening, then lets go.
void standbyTick(uint32_t now) {
  const Settings &c = cfg();
  standby_counting = c.standby && !jogging && !web_hold && !motor::isMoving() && !testActive() &&
                     !k1Busy(moonraker::snapshot());
  if (!standby_counting) {
    standby_busy_ms = now;
    return;
  }
  uint32_t wait = c.standby_min * 60000UL;
  if (now - standby_busy_ms < wait || now - state_since < wait || now - last_wake_ms < wait) return;
  motor::release();
  logf("standby: %s idle for %u min (not printing, not heating, too cold to extrude)", pn(),
       c.standby_min);
  enter(State::kStandby, String(pn()) + " idle: gear released");
}

// Seconds until READY stands by, -1 when it is not counting down.
int32_t standbyInS(uint32_t now) {
  if (state != State::kReady || !standby_counting) return -1;
  uint32_t idle = min(now - standby_busy_ms, min(now - state_since, now - last_wake_ms));
  uint32_t wait = cfg().standby_min * 60000UL;
  return idle >= wait ? 0 : (int32_t)((wait - idle + 999) / 1000);
}

void runState(const io::Sensors &s, uint32_t now) {
  bool in1_rise = s.in1 && !prev.in1;
  bool in1_fall = !s.in1 && prev.in1;
  bool in2_rise = s.in2 && !prev.in2;

  switch (state) {
    case State::kNoDriver:
    case State::kStuck:
    case State::kError:
    case State::kFree:  // handled by freeTick
      break;

    case State::kEmpty:
      if (s.in2) startPreload();
      else if (in1_rise) startInsert();
      break;

    case State::kInserting:
      if (s.in2) startPreload();
      else if (!s.in1) {
        motor::halt();
        enter(State::kEmpty, "filament removed");
      } else if (millis() - state_since > kInsertTimeoutMs) {
        motor::halt();
        enter(State::kInsertWait, "push filament further in");
      }
      break;

    case State::kInsertWait:
      if (s.in2) startPreload();
      else if (!s.in1) enter(State::kEmpty);
      break;

    case State::kPreload:
      if (!s.in2) {
        motor::halt();
        enter(s.in1 ? State::kInsertWait : State::kEmpty, "filament lost");
      } else if (!motor::isMoving()) {
        enter(State::kStaged, "press UP to load");
        io::beep(1);
      }
      break;

    case State::kStaged:  // buffer feeding paused here, e.g. to swap the PTFE tube
      if (!s.in2) {
        enter(s.in1 ? State::kInsertWait : State::kEmpty);
      } else if (moonraker::isPrinting()) {
        // The printer needs the buffer working; staying paused would let it pull filament
        // straight through the gear for the whole print.
        logf("%s started printing: leaving STAGED so the buffer works", pn());
        enter(State::kReady, String(pn()) + " printing: buffer on");
      } else {
        maybeStartFollowing();
      }
      break;

    case State::kLoading:
      if (!s.in2) {
        motor::halt();
        enter(State::kTailOut, "runout during load");
        break;
      }
      if (load_ext_armed) {
        moonraker::Snapshot k1 = moonraker::snapshot();
        if (!k1.fresh) {
          load_ext_baseline = false;  // reading is stale: re-baseline when it comes back
        } else if (!load_ext_baseline) {
          load_ext_baseline = true;
          load_ext_prev = k1.ext_sensor;
        } else if (k1.ext_sensor && !load_ext_prev) {  // rising edge anywhere in the load
          load_ext_prev = true;
          load_ext_armed = false;
          load_reached_ext = true;
          load_soft_phase = true;
          float measured = pos() - load_start_pos;
          logf("load: %s filament sensor triggered after %.0f mm, feeding %u mm more", pn(),
               measured, cfg().extruder_extra_mm);
          // The same idea as the stock firmware's calibration, but without pulling anything back:
          // a load that started at the gear has just measured the real path length.
          if (!cfg().lock_move_distance && load_from_staged && measured > 0) {
            uint16_t len = (uint16_t)(measured + cfg().path_margin_mm + 0.5f);
            if (len != cfg().load_mm) {
              logf("path calibrated: gear -> %s filament sensor is %.0f mm, load length now "
                   "%u mm (+%u margin)",
                   pn(), measured, len, cfg().path_margin_mm);
              settings::get().load_mm = len;
              settings::save();
            }
          }
          // A sensor next to the spool can be metres from the extruder: cover most of that at
          // the load speed and meet the extruder gently, like the rest of the load.
          float extra = cfg().extruder_extra_mm;
          load_target_mm = measured + extra;
          if (extra > kSoftLoadMm) {
            load_soft_phase = false;
            motor::moveMm(Dir::kFeed, extra - kSoftLoadMm, cfg().load_speed);
          } else if (extra > 0) {
            motor::moveMm(Dir::kFeed, extra, kSoftLoadSpeed);
          } else {
            motor::halt();
          }
          break;
        } else {
          load_ext_prev = k1.ext_sensor;
        }
      }
      if (!motor::isMoving()) {
        if (!load_soft_phase) {
          load_soft_phase = true;
          motor::moveMm(Dir::kFeed, kSoftLoadMm, kSoftLoadSpeed);
        } else if (load_full && load_expect_edge && !load_reached_ext) {
          // The whole distance went in and the printer never saw the filament: it is caught
          // somewhere, or the tip missed the sensor. Stop and let the user sort it out rather
          // than pretending the load worked.
          float fed = pos() - load_start_pos;
          enterAlarm(State::kError,
                     "fed " + String(fed, 0) + "mm, " + pn() + " never saw the filament");
          logf("load: fed %.0f mm and the %s filament sensor never triggered - check for a "
               "crooked or caught filament",
               fed, pn());
          String body = String("送完設定距離，") + pn() + " 仍然沒偵測到耗材，請手動檢查";
          moonraker::notify("PBF: 上料沒到位", body.c_str());
        } else {
          enter(State::kReady, "loaded " + String(pos() - load_start_pos, 0) + "mm" +
                                   (load_reached_ext ? " (at extruder)" : ""));
          io::beep(2);
        }
      } else {
        message = "LOAD " + String(pos() - load_start_pos, 0) + "/" + String(load_target_mm, 0) + "mm";
      }
      break;

    case State::kReady:
      if (!s.in2) {
        enterRunout(tail_passed ? "runout: printer handles it" : "filament gone");
        break;
      }
      if (in1_fall) {
        tail_passed = true;
        message = "spool empty soon";
        logf("tail passed the inlet sensor");
      }
      if (!jogging && !motor::isMoving() && bufferEmpty(s) && !btestOwnsMotor()) {
        uint32_t blocked_ms = topUpBlockRemainingMs(now);
        if (blocked_ms) {
          if (!maybeStartFollowing()) {
            message = "unload guard: no top-up, " + String((blocked_ms + 999) / 1000) + "s";
          }
        } else if (printerCanAnchor()) {
          buffer_blocked = false;
          startFeedingEpisode();
        } else if (!buffer_blocked) {
          buffer_blocked = true;
          message = anchor_waiting ? String("waiting for ") + pn() : String(pn()) + " extruder empty";
          logf("buffer: not topping up, %s %s", pn(), anchor_reason);
        }
      } else {
        maybeStartFollowing();
      }
      if (state == State::kReady) standbyTick(now);
      break;

    case State::kStandby:
      if (!s.in2) {
        enterRunout(tail_passed ? "runout: printer handles it" : "filament gone");
      } else if (!cfg().standby) {
        exitStandby("standby off");
      } else if (const char *why = k1Busy(moonraker::snapshot())) {
        exitStandby(String(pn()) + " " + why);
      }
      break;

    case State::kFollowing:
      updateFollowing(s);
      break;

    case State::kFeeding:
      updateFeeding(s, in1_fall);
      break;

    case State::kTailOut:
      if (in2_rise) startPreload();
      else if (in1_rise) startInsert();
      break;

    case State::kUnloading:
      updateUnloading(s);
      break;

    case State::kUnloaded:
      if (in2_rise) startPreload();
      else if (!s.in1 && !s.in2) enter(State::kEmpty);
      break;

  }
}

bool canJog() {
  if (motor::isMoving()) return false;
  return state == State::kStaged || state == State::kReady || state == State::kUnloaded ||
         state == State::kInsertWait || state == State::kTailOut || state == State::kEmpty;
}

// ---- Physical keys ----
//   UP   click: feed by distance (load_mm)           hold: feed while held
//   BACK click: retract by distance (unload_mm)      hold: retract while held
//        double click: unload until the tip leaves the gear
//        click, then hold within 2 s: the distance retract turns into a hold retract
//   Any key while something is moving stops it immediately (the rest of that press is ignored).
//   Any key while an alarm is shown clears it.

// A user-started or automatic sequence that a key press should stop (buffer feeding excluded).
bool actionRunning() {
  return state == State::kInserting || state == State::kPreload || state == State::kLoading ||
         state == State::kUnloading || state == State::kFollowing || testActive() ||
         (motor::isMoving() && state != State::kFeeding);
}

void stopAction(const char *why) {
  stopTests(why);
  const io::Sensors &s = io::sensors();
  web_hold = false;
  jogging = false;
  unload_from_click = false;
  switch (state) {
    case State::kInserting:
    case State::kPreload:
      motor::halt();
      enter(s.in2 ? State::kStaged : State::kInsertWait, why);
      break;
    case State::kLoading:
      motor::stop();
      enter(State::kReady, why);
      break;
    case State::kUnloading:
      motor::stop();
      enter(s.in2 ? State::kStaged : State::kUnloaded, why);
      break;
    case State::kFeeding:  // a test was stopped during one of its top-ups
      motor::stop();
      enter(State::kReady, why);
      break;
    default:
      motor::stop();  // a jog in an idle state
      logf("%s", why);
      break;
  }
}

bool filamentAtPrinter() { return state == State::kReady || state == State::kFeeding; }

// Starts a manual move that lasts while a key/button is held. With the filament already at
// the printer a feed is capped, so a held key cannot grind the filament against the extruder.
void startManualHold(bool feed, float speed, const char *source) {
  bool capped = feed && filamentAtPrinter();
  if (state == State::kFeeding) {
    motor::halt();
    enter(State::kReady);
  }
  if (!canJog()) return;
  jogging = true;
  applyMovePressure();
  if (capped) motor::moveMm(Dir::kFeed, kLoadedHoldMaxMm, speed);
  else motor::run(feed ? Dir::kFeed : Dir::kRetract, speed);
  logf("%s hold: %s%s", source, feed ? "feed" : "retract", capped ? " (max 20 mm, filament at printer)" : "");
}

void startKeyHold(bool feed) { startManualHold(feed, kJogSpeed, "key"); }

void upClick() {
  const io::Sensors &s = io::sensors();
  if (filamentAtPrinter()) {
    logf("UP click ignored: filament already at the printer, hold UP to feed (max 20 mm)");
    message = "hold UP to feed (max 20mm)";
    return;
  }
  if (s.in2) {
    logf("UP click: feed %u mm", cfg().load_mm);
    startLoad(cfg().load_mm, true);
  } else if (s.in1) {
    startInsert();  // filament at the inlet but not in the gear yet: grab it
  }
}

void backSingleClick() {
  if (!io::sensors().in2) {
    logf("BACK click: no filament in the gear");
    return;
  }
  if (motor::isMoving()) motor::halt();
  logf("BACK click: retract %u mm", cfg().unload_mm);
  startUnload(cfg().unload_mm, false);
  unload_from_click = true;
  unload_click_ms = millis();
}

void backDoubleClick() {
  if (motor::isMoving()) motor::halt();
  logf("BACK double click: unload until empty");
  unload_from_click = false;
  startUnload(fullUnloadLimit(), true);
}

void handleKey(io::KeyEvent e) {
  using io::KeyEvent;
  bool is_up = e == KeyEvent::kUpDown || e == KeyEvent::kUpShort || e == KeyEvent::kUpHoldStart ||
               e == KeyEvent::kUpHoldEnd;
  bool &swallow = is_up ? swallow_up : swallow_back;

  if (state == State::kStuck || state == State::kError) {
    if (e == KeyEvent::kUpDown || e == KeyEvent::kBackDown) {
      io::alarm(false);
      settle();
      swallow = true;
    }
    return;
  }

  if (state == State::kFree) {
    if (e == KeyEvent::kUpDown || e == KeyEvent::kBackDown) {
      exitFree("key pressed", true);
      swallow = true;
    }
    return;
  }

  // The press only wakes it: the screen was dim, so the user cannot know what it would have done.
  if (state == State::kStandby) {
    if (e == KeyEvent::kUpDown || e == KeyEvent::kBackDown) {
      exitStandby("key pressed");
      swallow = true;
    }
    return;
  }

  if (locked()) {
    if (e == KeyEvent::kUpDown || e == KeyEvent::kBackDown) {
      if (actionRunning()) {
        stopAction("stopped by key");
      } else {
        message = lockText();
        logf("key ignored: %s", lockReason().c_str());
        io::beep(1, 40, 0);
      }
      swallow = true;
    } else if (e != KeyEvent::kUpHoldStart && e != KeyEvent::kBackHoldStart) {
      swallow = false;  // end of the press
    }
    up_click_pending = back_click_pending = false;
    return;
  }

  switch (e) {
    case KeyEvent::kUpDown:
    case KeyEvent::kBackDown:
      if (!is_up && state == State::kUnloading && unload_from_click &&
          millis() - unload_click_ms < kClickToHoldMs) {
        back_convert_pending = true;  // wait: short press = stop, hold = switch to hold retract
        return;
      }
      if (actionRunning()) {
        stopAction("stopped by key");
        swallow = true;
      }
      return;

    case KeyEvent::kUpShort:
    case KeyEvent::kBackShort:
      if (swallow) {
        swallow = false;
        return;
      }
      if (!is_up && back_convert_pending) {
        back_convert_pending = false;
        stopAction("stopped by key");
        return;
      }
      if (is_up && filamentAtPrinter()) {
        if (up_click_pending && millis() - up_click_ms <= kDoubleClickMs) {
          up_click_pending = false;
          motor::halt();
          jogging = false;
          enter(State::kStaged, "buffer paused (tube swap OK)");
          logf("UP double click: back to STAGED");
        } else {
          up_click_pending = true;  // a lone click is ignored in READY (decided in serviceClicks)
          up_click_ms = millis();
        }
      } else if (is_up) {
        upClick();
      } else if (back_click_pending && millis() - back_click_ms <= kDoubleClickMs) {
        back_click_pending = false;
        backDoubleClick();
      } else {
        back_click_pending = true;  // decided in update(): single click unless a second one follows
        back_click_ms = millis();
      }
      return;

    case KeyEvent::kUpHoldStart:
    case KeyEvent::kBackHoldStart:
      if (swallow) return;
      if (!is_up && back_convert_pending) {
        back_convert_pending = false;
        unload_from_click = false;
        motor::halt();
        enter(io::sensors().in2 ? State::kStaged : State::kUnloaded, "switched to hold");
      }
      if (!is_up) back_click_pending = false;  // click then hold = hold
      else up_click_pending = false;
      startKeyHold(is_up);
      return;

    case KeyEvent::kUpHoldEnd:
    case KeyEvent::kBackHoldEnd:
      if (swallow) {
        swallow = false;
        return;
      }
      if (jogging && !web_hold) {
        jogging = false;
        motor::stop();
        logf("key hold released, moved %.1f mm", motor::moveDistanceMm());
      }
      return;

    default:
      return;
  }
}

void serviceClicks() {
  if (up_click_pending && millis() - up_click_ms > kDoubleClickMs && !io::keyUpDown()) {
    up_click_pending = false;
    upClick();  // logs that a single click is ignored while the filament is at the printer
  }
  if (back_click_pending && millis() - back_click_ms > kDoubleClickMs && !io::keyBackDown()) {
    back_click_pending = false;
    backSingleClick();
  }
}

void handleKeys() {
  io::KeyEvent e;
  while ((e = io::nextKey()) != io::KeyEvent::kNone) {
    last_wake_ms = millis();
    if (ui::menuActive()) {
      switch (ui::menuKey(e)) {
        case ui::MenuAction::kPressureTest: startPressureTest(); break;
        case ui::MenuAction::kBufferTest: startBufferTest(); break;
        case ui::MenuAction::kFullTest: startFullTest(); break;
        case ui::MenuAction::kExit:
          applyPressure();
          message = "settings saved";
          settings::print(Serial);
          break;
        case ui::MenuAction::kNone: break;
      }
      continue;
    }
    if (e == io::KeyEvent::kBothLong) {
      if (!motor::isMoving() && !testActive()) {
        jogging = false;
        ui::menuOpen();
      }
      continue;
    }
    handleKey(e);
  }
}

void logSensorEdges(const io::Sensors &s) {
  if (s.in1 != prev.in1) logf("IN1 %d", s.in1);
  if (s.in2 != prev.in2) logf("IN2 %d", s.in2);
  // The carriage toggles on every top-up; the summary line covers it unless verbose is on.
  if (verbose && s.buf_rest != prev.buf_rest) logf("BUF %s (pos %.1f)", s.buf_rest ? "home" : "out", pos());
  if (verbose && s.buf_pos != prev.buf_pos) logf("BUF_POS %d", s.buf_pos);
}

void releaseIfIdle(uint32_t now) {
  if (motor::isMoving() || jogging) {
    last_motion_ms = now;
    return;
  }
  // In READY the gear should keep holding the filament, otherwise the printer just pulls it
  // straight through the feeder and the buffer never reacts.
  bool hold = grip_after_free || state == State::kFeeding || testActive() ||
              (state == State::kReady && !cfg().idle_release);
  if (hold) motor::hold();
  else if (now - last_motion_ms > kReleaseDelayMs) motor::release();
}

String currentHint() {
  if (locked() && (state == State::kReady || state == State::kFeeding)) return lockText();
  return hintFor(state);
}

// READY is dim; buffer feeding from READY brightens it for a while; key presses wake it.
uint8_t brightnessFor(uint32_t now) {
  const Settings &c = cfg();
  if (ui::menuActive()) return c.brightness;
  if (state == State::kFeeding) {
    last_active_ms = now;
    return c.brightness_active;
  }
  if (state != State::kReady && state != State::kStandby) return c.brightness;
  if (last_active_ms && now - last_active_ms < kActiveBrightMs) return c.brightness_active;
  if (last_wake_ms && now - last_wake_ms < kWakeBrightMs) return c.brightness;
  return c.brightness_ready;
}

// Buffer top-ups (READY <-> FEEDING, every few seconds while printing) keep the READY screen and
// fill the arrow beside the title instead, so nothing else on the screen flickers.
bool arrowShown() { return state == State::kReady || state == State::kFeeding; }

// How full the arrow is: the share of the expected top-up fed so far (-1 = no arrow).
float feedArrow(uint32_t now) {
  if (!arrowShown()) return -1;
  if (state == State::kReady) {
    return arrow_full_until_ms && (int32_t)(arrow_full_until_ms - now) > 0 ? 1.0f : 0.0f;
  }
  float park = max<float>(cfg().overfeed_mm, 1.0f);
  if (!overfeeding) {  // still pushing the carriage off its rest stop: guess from the last top-up
    float home = last_topup.home_mm > 0.5f ? last_topup.home_mm : 2.0f;
    return min(pos() - episode_start_pos, home) / (home + park);
  }
  float home = overfeed_start_pos - episode_start_pos;
  return min(1.0f, (home + pos() - overfeed_start_pos) / (home + park));
}

bool arrowAnimating(uint32_t now) {
  if (state == State::kFeeding) return true;
  // one more frame after the full hold ends, to empty it without waiting for a full frame
  return state == State::kReady && arrow_full_until_ms &&
         (int32_t)(now - arrow_full_until_ms) < (int32_t)kArrowPeriodMs * 2;
}

void drawUi(uint32_t now) {
  if (now - last_ui_ms < kUiPeriodMs) {
    if (!ui::menuActive() && arrowAnimating(now) && now - last_arrow_ms >= kArrowPeriodMs) {
      last_arrow_ms = now;
      ui::drawFeedArrow(feedArrow(now));
    }
    return;
  }
  last_ui_ms = now;
  last_arrow_ms = now;
  ui::setBrightnessPct(brightnessFor(now));
  if (ui::menuActive()) {
    ui::drawMenu();
    return;
  }
  bool feeding = state == State::kFeeding;
  ui::MainScreen screen;
  screen.title = feeding ? stateName(State::kReady) : stateName(state);
  screen.detail = feeding && !testActive() ? feed_prev_msg : testMsg();
  screen.hint = currentHint();
  screen.ip_in_hint = arrowShown() && !locked();
  screen.feed = feedArrow(now);
  ui::drawMain(screen);
}

void verboseLog(uint32_t now) {
  if (!verbose || !motor::isMoving() || now - last_verbose_ms < kVerbosePeriodMs) return;
  last_verbose_ms = now;
  logf("pos %.1f mm, SG %u, comb edges %lu", pos(), motor::sgResult(),
       (unsigned long)io::bufPosEdges());
}

void stopWebHold(const char *why) {
  if (!web_hold) return;
  web_hold = false;
  jogging = false;
  motor::stop();
  logf("hold released (%s), moved %.1f mm", why, motor::moveDistanceMm());
}

// Manual moves are refused while an alarm, calibration or missing driver needs attention.
bool refusedWhileLocked(const char *what) {
  if (!locked()) return false;
  logf("%s refused: %s", what, lockReason().c_str());
  message = lockText();
  return true;
}

bool busyForManual() {
  return state == State::kStuck || state == State::kError || testActive() ||
         state == State::kNoDriver;
}

}  // namespace

void begin() {
  prev = io::sensors();
  if (!motor::driverOk()) enter(State::kNoDriver, "no motor power (24V?)");
  else settle();
}

void update() {
  const io::Sensors s = io::sensors();
  uint32_t now = millis();
  {  // lifetime counters (kept in flash, see stats.cpp)
    static float last_pos = 0;
    static uint32_t last_stats_ms = 0;
    static moonraker::PrintState last_print = moonraker::PrintState::kUnknown;
    float p = pos();
    if (p != last_pos) {
      stats::addMoved(p - last_pos);
      last_pos = p;
    }
    if (now - last_stats_ms > 1000) {
      last_stats_ms = now;
      stats::setUartHiccups(motor::uartHiccups());
      moonraker::PrintState ps = moonraker::snapshot().print_state;
      if (ps != last_print) {
        // A print just ended: a good moment to put the counters in flash.
        if (last_print == moonraker::PrintState::kPrinting) stats::flush();
        last_print = ps;
      }
    }
  }
  logSensorEdges(s);
  logLockHeld();

  if (!motor::driverOk()) {
    if (state != State::kNoDriver) {
      motor::halt();
      jogging = false;
      enter(State::kNoDriver, "no motor power (24V?)");
    }
  } else if (state == State::kNoDriver) {
    settle();
  }

  handleKeys();
  serviceClicks();
  if ((int32_t)(millis() - web_hold_deadline) > 0) {
    web_hold_refused = false;  // the refused press has ended (or its release was lost)
    if (web_hold) stopWebHold("no keep-alive");
  }
  freeTick(now);
  if (!ui::menuActive()) runState(s, now);
  ptestTick(now);
  btestTick(now);
  flushBufferSummary(now);
  releaseIfIdle(now);
  prev = s;
  drawUi(now);
  verboseLog(now);
}

void cmdFree(bool on) {
  if (on) {
    if (state == State::kFree) return;
    if (refusedWhileLocked("release the gear")) return;
    if (!canFree()) {
      logf("cannot release the gear in %s", stateName(state));
      return;
    }
    free_blocked = false;
    enterFree("gear released by hand", false);
  } else if (state == State::kFree) {
    exitFree("gripped by hand", true);
  }
}

void cmdLoad() {
  if (refusedWhileLocked("load")) return;
  if (testActive()) {
    logf("load ignored: a test is running (press stop first)");
    return;
  }
  if (!io::sensors().in2 || motor::isMoving()) {
    logf("load needs filament at the gear and the motor idle");
    return;
  }
  startLoad(cfg().load_mm, true);
}

void cmdUnload() {
  if (refusedWhileLocked("unload")) return;
  if (testActive()) {
    logf("unload ignored: a test is running (press stop first)");
    return;
  }
  if (motor::isMoving()) motor::halt();
  startUnload(fullUnloadLimit(), true);
}

void cmdFeedDistance(float mm) {
  if (refusedWhileLocked("feed by distance")) return;
  if (busyForManual() || motor::isMoving() || !io::sensors().in2 || mm <= 0) {
    logf("feed by distance refused (needs filament at the gear, motor idle, no alarm)");
    return;
  }
  logf("feed by distance %.0f mm", mm);
  startLoad(mm, false);
}

void cmdUnloadDistance(float mm) {
  if (refusedWhileLocked("retract by distance")) return;
  if (busyForManual() || mm <= 0) {
    logf("retract by distance refused");
    return;
  }
  if (motor::isMoving()) motor::halt();
  jogging = web_hold = false;
  logf("retract by distance %.0f mm", mm);
  startUnload(mm, false);
}

void cmdHold(bool feed) {
  web_hold_deadline = millis() + kWebHoldTimeoutMs;
  if (web_hold || web_hold_refused) return;  // keep-alive of an accepted or refused press
  last_wake_ms = millis();
  if (state == State::kStandby) exitStandby("web hold");
  if (refusedWhileLocked("hold")) {
    web_hold_refused = true;
    return;
  }
  if (!busyForManual()) startManualHold(feed, kWebHoldSpeed, "web");
  web_hold = jogging;
  if (!web_hold) {
    web_hold_refused = true;
    logf("hold refused in state %s", stateName(state));
  }
}

void cmdHoldStop() {
  web_hold_refused = false;
  stopWebHold("released");
}

void cmdStop() {
  stopTests("stopped");
  motor::halt();
  jogging = false;
  web_hold = false;
  io::alarm(false);
  settle();
}

void cmdJog(float mm) {
  if (refusedWhileLocked("jog")) return;
  if (motor::isMoving() || mm == 0 || testActive()) {
    logf("jog ignored (%s)", mm == 0 ? "0 mm" : testActive() ? "test running" : "motor busy");
    return;
  }
  logf("jog %+.0f mm", mm);
  applyMovePressure();
  motor::moveMm(mm > 0 ? Dir::kFeed : Dir::kRetract, fabsf(mm), kJogSpeed);
}

void cmdPressureTest() {
  if (refusedWhileLocked("pressure test")) return;
  startPressureTest();
}

void cmdBufferTest() {
  if (refusedWhileLocked("buffer test")) return;
  startBufferTest();
}

void cmdFullTest() {
  if (refusedWhileLocked("full test")) return;
  startFullTest();
}

void cmdNotifyTest() {
  logf("sending a test notification to %s (Mobileraker)", pn());
  moonraker::notify("PBF 測試", "喂竹器推播正常");
}

void setVerbose(bool on) { verbose = on; }

// Every web action except the hold keep-alive and hold_stop comes through here, so it also ends
// standby.
void wakeScreen() {
  last_wake_ms = millis();
  if (state == State::kStandby) exitStandby("web action");
}

void printStatus(Print &out) {
  const io::Sensors &s = io::sensors();
  out.printf("state=%s msg=\"%s\" IN1=%d IN2=%d BUF=%s comb_edges=%lu pos=%.1fmm tail=%d\n",
             stateName(state), message.c_str(), s.in1, s.in2, s.buf_rest ? "home" : "out",
             (unsigned long)io::bufPosEdges(), pos(), tail_passed);
  out.printf("motor: %s moving=%d\n", motor::describeStatus().c_str(), motor::isMoving());
  settings::print(out);
}

void appendStatusJson(String &out) {
  const io::Sensors &s = io::sensors();
  out += "\"state\":";
  logbuf::appendJsonString(out, stateName(state));
  out += ",\"msg\":";
  logbuf::appendJsonString(out, testMsg().c_str());
  out += ",\"ptest\":" + String(ptestActive() ? 1 : 0);
  out += ",\"test\":";
  logbuf::appendJsonString(out, full_test ? "full" : ptestActive() ? "pressure" : btestActive() ? "buffer" : "");
  const TestResult &r = test_result;
  out += ",\"result\":{\"seq\":" + String(r.seq) + ",\"kind\":";
  logbuf::appendJsonString(out, r.kind);
  out += ",\"ok\":" + String(r.ok ? 1 : 0) + ",\"code\":";
  logbuf::appendJsonString(out, r.code);
  out += ",\"detail\":";
  logbuf::appendJsonString(out, r.detail.c_str());
  out += ",\"travel_mm\":" + String(r.travel_mm, 1) + ",\"home_mm\":" + String(r.home_mm, 1) +
         ",\"overfeed_old\":" + String(r.overfeed_old) + ",\"overfeed\":" + String(r.overfeed) +
         ",\"pressure\":" + String(r.pressure) +
         ",\"pressure_ma\":" + String(r.pressure ? hw::pressureLevelToMa(r.pressure) : 0) +
         ",\"hold_ma\":" + String(r.hold_ma) +
         ",\"idle_release_fixed\":" + String(r.idle_release_fixed ? 1 : 0) + "}";
  out += ",\"hint\":";
  logbuf::appendJsonString(out, currentHint().c_str());
  out += ",\"in1\":" + String(s.in1 ? 1 : 0);
  out += ",\"in2\":" + String(s.in2 ? 1 : 0);
  out += ",\"buf_rest\":" + String(s.buf_rest ? 1 : 0);
  out += ",\"buf_full\":" + String(bufferFull(s) ? 1 : 0);
  out += ",\"comb_edges\":" + String(io::bufPosEdges());
  out += ",\"topup\":{\"home_mm\":" + String(last_topup.home_mm, 1) +
         ",\"over_mm\":" + String(last_topup.over_mm, 1) +
         ",\"last_edge_mm\":" + String(last_topup.last_edge_mm, 1) +
         ",\"edges\":" + String(last_topup.edges) +
         ",\"stalled\":" + String(last_topup.stalled ? 1 : 0) + "}";
  out += ",\"pos\":" + String(pos(), 1);
  out += ",\"moving\":" + String(motor::isMoving() ? 1 : 0);
  out += ",\"move_mm\":" + String(motor::moveDistanceMm(), 1);
  out += ",\"driver\":" + String(motor::driverOk() ? 1 : 0);
  out += ",\"uart_err\":" + String(motor::uartHiccups());
  out += ",\"stats\":";
  stats::appendJson(out);
  out += ",\"drv_warm\":" + String(motor::driverWarm() ? 1 : 0);
  out += ",\"current_ma\":" + String(motor::currentMa());
  out += ",\"alarm\":" + String(io::alarmActive() ? 1 : 0);
  out += ",\"menu\":" + String(ui::menuActive() ? 1 : 0);
  moonraker::Snapshot k1 = moonraker::snapshot();
  out += ",\"k1\":";
  logbuf::appendJsonString(out, moonraker::statusText().c_str());
  out += ",\"k1_printing\":" + String(k1.fresh && k1.print_state == moonraker::PrintState::kPrinting ? 1 : 0);
  out += ",\"k1_toolhead\":" + String(k1.fresh && k1.ext_sensor ? 1 : 0);
  out += ",\"k1_toolhead_sensor\":" + String(k1.ext_sensor_present ? 1 : 0);
  out += ",\"sensors\":" + moonraker::sensorsJson();
  out += ",\"locked\":" + String(locked() ? 1 : 0);
  out += ",\"lock_offline\":" + String(locked() && !k1.fresh ? 1 : 0);
  out += ",\"k1_pbf_macro\":" + String(k1.pbf_macro ? 1 : 0);
  out += ",\"k1_pbf_display\":" + String(k1.pbf_display ? 1 : 0);
  out += ",\"k1_pbf_m117\":" + String(k1.pbf_from_m117 ? 1 : 0);
  out += ",\"k1_pbf_mode\":" + String(k1.pbf_mode);
  out += ",\"free\":" + String(state == State::kFree ? 1 : 0);
  out += ",\"can_free\":" + String(canFree() ? 1 : 0);
  out += ",\"free_blocked\":" + String(free_blocked ? 1 : 0);
  out += ",\"topup_block_s\":" + String((topUpBlockRemainingMs(millis()) + 999) / 1000);
  out += ",\"standby_in_s\":" + String(standbyInS(millis()));
  out += ",\"free_src\":";
  logbuf::appendJsonString(out, state != State::kFree ? ""
                                : free_from_k1 ? "marker" : free_from_retract ? "retract" : "manual");
}

}  // namespace feeder
