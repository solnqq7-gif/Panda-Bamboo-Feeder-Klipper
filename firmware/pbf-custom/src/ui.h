// OLED screens and the two-key settings menu.
#pragma once

#include <Arduino.h>

#include "io.h"

namespace ui {

struct MainScreen {
  String title;   // state, large font
  String detail;  // progress or message
  String hint;    // what the keys do now
  bool ip_in_hint = false;  // the hint line starts with the IP, so line 3 leaves it out
  float feed = -1;          // feed arrow beside the title: -1 = hidden, 0..1 = share filled
};

enum class MenuAction { kNone, kExit, kPressureTest, kBufferTest, kFullTest };

void begin();
void setBrightnessPct(uint8_t pct);  // cached, only sends the command on change
void drawMain(const MainScreen &screen);
// Redraws only the feed arrow corner and sends just those tiles, so the arrow can animate
// between full frames without the cost of a whole-screen refresh.
void drawFeedArrow(float feed);

void menuOpen();
void menuClose();  // saves pending settings; also used when a print starts
bool menuActive();
MenuAction menuKey(io::KeyEvent e);  // settings are saved when the menu closes
void drawMenu();

}  // namespace ui
