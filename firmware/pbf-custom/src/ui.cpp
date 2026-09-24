#include "ui.h"

#include <U8g2lib.h>

#include "config.h"
#include "motor.h"
#include "net.h"
#include "settings.h"

namespace ui {
namespace {

U8G2_SSD1306_128X64_NONAME_F_4W_SW_SPI oled(U8G2_R0, pins::kOledSclk, pins::kOledMosi, pins::kOledCs,
                                            pins::kOledDc, U8X8_PIN_NONE);

enum Item : uint8_t {
  kPressure,
  kPressureTest,
  kBufferTest,
  kFullTest,
  kBuzzer,
  kPreload,
  kLoadLen,
  kStuckLen,
  kIdleRelease,
  kBrightness,
  kBrightnessReady,
  kExit,
  kItemCount,
};

const char *const kLabels[kItemCount] = {
    "Pressure", "Pressure test", "Buffer test", "Full test", "Buzzer", "Preload",
    "Load length", "Stuck length", "Idle release", "Brightness", "Dim (READY)", "Exit",
};

bool menu_active = false;
int current_contrast = -1;
uint8_t cursor = 0;

String itemValue(uint8_t item) {
  const Settings &s = settings::get();
  switch (item) {
    case kPressure: return "L" + String(s.pressure_level) + " " + String(hw::pressureLevelToMa(s.pressure_level)) + "mA";
    case kPressureTest:
    case kBufferTest:
    case kFullTest: return "run";
    case kBuzzer: return s.buzzer ? "ON" : "OFF";
    case kPreload: return String(s.preload_mm) + "mm";
    case kLoadLen: return String(s.load_mm) + "mm";
    case kStuckLen: return String(s.stuck_mm) + "mm";
    case kIdleRelease: return s.idle_release ? "ON" : "OFF";
    case kBrightness: return String(s.brightness) + "%";
    case kBrightnessReady: return String(s.brightness_ready) + "%";
    default: return "";
  }
}

// Single "change" key, so numeric values step up and wrap around.
uint16_t stepWrap(uint16_t v, uint16_t step, uint16_t lo, uint16_t hi) {
  v += step;
  return v > hi ? lo : v;
}

// Feed arrow in the top right corner, beside the state title. Only shown with "READY", so it
// never collides with a long title. It fits in tiles 8..15 x 0..1 (x 64..127, y 0..15).
constexpr int kArrowX0 = 68;    // tail
constexpr int kArrowXH = 110;   // back of the head
constexpr int kArrowX1 = 126;   // tip
constexpr int kArrowCy = 7;     // centre row
constexpr int kArrowHead = 6;   // half height of the head
constexpr int kArrowShaft = 4;  // half height of the shaft

// Outline, then the fill from the tail up to `feed`, kept 1 px clear of the outline like the
// sensor boxes.
void drawArrow(float feed) {
  const int st = kArrowCy - kArrowShaft, sb = kArrowCy + kArrowShaft;
  const int ht = kArrowCy - kArrowHead, hb = kArrowCy + kArrowHead;
  oled.drawHLine(kArrowX0, st, kArrowXH - kArrowX0 + 1);
  oled.drawHLine(kArrowX0, sb, kArrowXH - kArrowX0 + 1);
  oled.drawVLine(kArrowX0, st, sb - st + 1);
  oled.drawVLine(kArrowXH, ht, st - ht + 1);
  oled.drawVLine(kArrowXH, sb, hb - sb + 1);
  oled.drawLine(kArrowXH, ht, kArrowX1, kArrowCy);
  oled.drawLine(kArrowXH, hb, kArrowX1, kArrowCy);
  if (feed <= 0) return;
  const int first = kArrowX0 + 2, last = kArrowX1 - 3;
  const int to = first + (int)lroundf(min(feed, 1.0f) * (last - first));
  for (int x = first; x <= to; x++) {
    int top = st + 2, bottom = sb - 2;
    if (x > kArrowXH + 1) {  // inside the head: follow its slanted edges
      float h = (float)kArrowHead * (kArrowX1 - x) / (kArrowX1 - kArrowXH);
      top = (int)lroundf(kArrowCy - h) + 2;
      bottom = (int)lroundf(kArrowCy + h) - 2;
    }
    if (bottom >= top) oled.drawVLine(x, top, bottom - top + 1);
  }
}

// A 7x7 box, with a 3x3 block inside it (1 px clear) when on.
void drawCheck(int x, int baseline, bool on) {
  oled.drawFrame(x, baseline - 7, 7, 7);
  if (on) oled.drawBox(x + 2, baseline - 5, 3, 3);
}

void drawSensors(int y) {
  const io::Sensors &s = io::sensors();
  oled.drawStr(0, y, "IN1");
  drawCheck(17, y, s.in1);
  oled.drawStr(28, y, "IN2");
  drawCheck(45, y, s.in2);
  oled.drawStr(57, y, s.buf_rest ? "BUF:home" : "BUF:out");
}

}  // namespace

void begin() {
  oled.begin();
  oled.getU8x8()->x_offset = hw::kOledXOffset;
}

void setBrightnessPct(uint8_t pct) {
  int contrast = (min<int>(pct, 100) * 255 + 50) / 100;
  if (contrast == current_contrast) return;
  current_contrast = contrast;
  oled.setContrast(contrast);
}

void drawMain(const MainScreen &screen) {
  const Settings &s = settings::get();
  oled.clearBuffer();
  oled.setFont(u8g2_font_7x14B_tf);
  oled.drawStr(0, 12, screen.title.c_str());
  if (screen.feed >= 0) drawArrow(screen.feed);
  oled.setFont(u8g2_font_5x8_tf);
  oled.drawStr(0, 24, screen.detail.c_str());
  String link = !motor::driverOk() ? String("NO 24V") : screen.ip_in_hint ? String() : net::shortStatus();
  char line[40];
  snprintf(line, sizeof(line), "L%u %umA %s", s.pressure_level, hw::pressureLevelToMa(s.pressure_level),
           link.c_str());
  oled.drawStr(0, 36, line);
  drawSensors(47);
  oled.drawHLine(0, 51, 128);
  String hint = screen.ip_in_hint ? net::shortStatus() + " " + screen.hint : screen.hint;
  oled.drawStr(0, 61, hint.c_str());
  oled.sendBuffer();
}

void drawFeedArrow(float feed) {
  oled.setDrawColor(0);
  oled.drawBox(64, 0, 64, 16);
  oled.setDrawColor(1);
  if (feed >= 0) drawArrow(feed);
  oled.updateDisplayArea(8, 0, 8, 2);
}

void menuOpen() {
  menu_active = true;
  cursor = 0;
}

bool menuActive() { return menu_active; }

MenuAction menuKey(io::KeyEvent e) {
  if (!menu_active) return MenuAction::kNone;
  Settings &s = settings::get();
  if (e == io::KeyEvent::kBothLong) {
    menu_active = false;
    settings::save();
    return MenuAction::kExit;
  }
  if (e == io::KeyEvent::kUpShort) {
    cursor = (cursor + 1) % kItemCount;
    return MenuAction::kNone;
  }
  if (e != io::KeyEvent::kBackShort) return MenuAction::kNone;
  switch (cursor) {
    case kPressure:
      s.pressure_level = s.pressure_level >= hw::kMaxPressureLevel ? hw::kMinPressureLevel : s.pressure_level + 1;
      motor::setCurrentMa(hw::pressureLevelToMa(s.pressure_level), s.hold_ma);
      break;
    case kPressureTest:
    case kBufferTest:
    case kFullTest:
      menu_active = false;
      settings::save();
      return cursor == kPressureTest ? MenuAction::kPressureTest
             : cursor == kBufferTest ? MenuAction::kBufferTest
                                     : MenuAction::kFullTest;
    case kBuzzer:
      s.buzzer = !s.buzzer;
      if (s.buzzer) io::testBeep();
      break;
    case kPreload: s.preload_mm = stepWrap(s.preload_mm, 10, 0, 200); break;
    case kLoadLen: s.load_mm = stepWrap(s.load_mm, 50, 50, 1500); break;
    case kStuckLen: s.stuck_mm = stepWrap(s.stuck_mm, 10, 30, 200); break;
    case kIdleRelease: s.idle_release = !s.idle_release; break;
    case kBrightness: s.brightness = stepWrap(s.brightness, 10, 10, 100); break;
    case kBrightnessReady: s.brightness_ready = stepWrap(s.brightness_ready, 5, 0, 50); break;
    case kExit:
      menu_active = false;
      settings::save();
      return MenuAction::kExit;
  }
  return MenuAction::kNone;
}

void drawMenu() {
  oled.clearBuffer();
  oled.setFont(u8g2_font_6x10_tf);
  oled.drawStr(0, 9, "SETTINGS");
  oled.drawHLine(0, 11, 128);
  // Show 4 items around the cursor.
  uint8_t first = cursor < 2 ? 0 : min<uint8_t>(cursor - 1, kItemCount - 4);
  for (uint8_t row = 0; row < 4; row++) {
    uint8_t item = first + row;
    if (item >= kItemCount) break;
    int y = 22 + row * 10;
    if (item == cursor) {
      oled.drawBox(0, y - 8, 128, 10);
      oled.setDrawColor(0);
    }
    oled.drawStr(2, y, kLabels[item]);
    String v = itemValue(item);
    oled.drawStr(126 - oled.getStrWidth(v.c_str()), y, v.c_str());
    oled.setDrawColor(1);
  }
  oled.setFont(u8g2_font_5x8_tf);
  oled.drawStr(0, 63, "UP:next  BACK:change");
  oled.sendBuffer();
}

}  // namespace ui
