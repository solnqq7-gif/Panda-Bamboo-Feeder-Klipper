// Panda Bamboo Feeder custom firmware (for any Klipper printer with Moonraker)
//
// Buffer / load-assist only, no 5-way hub. Keys: UP = load / feed, BACK = unload,
// hold a key = jog, both keys 1 s = settings menu.
//
// Serial console (115200 baud, one command per line):
//   help | status | set <name> <value> | defaults | load | unload | stop | jog <mm>
//   ptest (pressure test) | btest (buffer test) | fulltest (both) | beep | verbose on|off | reboot
#include <Arduino.h>

#include "config.h"
#include "feeder.h"
#include "io.h"
#include "logbuf.h"
#include "moonraker.h"
#include "motor.h"
#include "net.h"
#include "settings.h"
#include "stats.h"
#include "ui.h"

namespace {

String line;

void printHelp() {
  Serial.println("commands: help status set <name> <value> defaults load unload stop jog <mm>");
  Serial.println("          ptest btest fulltest beep free grip verbose on|off reboot");
  Serial.println("settings: pressure buzzer steps_mm preload_mm load_mm stuck_mm idle_release");
  Serial.println("          invert_dir load_speed buffer_speed unload_speed standby standby_min");
  Serial.println("          printer_name printer_host printer_port sensor (auto | none | name)");
}

void handleCommand(String cmd) {
  cmd.trim();
  if (cmd.isEmpty()) return;
  int space = cmd.indexOf(' ');
  String word = space < 0 ? cmd : cmd.substring(0, space);
  String rest = space < 0 ? "" : cmd.substring(space + 1);
  rest.trim();

  if (word == "help") printHelp();
  else if (word == "status") feeder::printStatus(Serial);
  else if (word == "set") {
    int sp = rest.indexOf(' ');
    if (sp < 0) {
      Serial.println("usage: set <name> <value>");
    } else if (settings::set(rest.substring(0, sp), rest.substring(sp + 1))) {
      motor::setCurrentMa(hw::pressureLevelToMa(settings::get().pressure_level), settings::get().hold_ma);
      settings::print(Serial);
    } else {
      Serial.println("unknown setting");
    }
  } else if (word == "defaults") {
    settings::resetToDefaults();
    settings::print(Serial);
  } else if (word == "load") feeder::cmdLoad();
  else if (word == "unload") feeder::cmdUnload();
  else if (word == "stop") feeder::cmdStop();
  else if (word == "free") feeder::cmdFree(true);
  else if (word == "grip") feeder::cmdFree(false);
  else if (word == "jog") feeder::cmdJog(rest.toFloat());
  else if (word == "ptest") feeder::cmdPressureTest();
  else if (word == "btest") feeder::cmdBufferTest();
  else if (word == "fulltest") feeder::cmdFullTest();
  else if (word == "beep") io::testBeep();
  else if (word == "verbose") feeder::setVerbose(rest == "on");
  else if (word == "reboot") ESP.restart();
  else Serial.printf("unknown command '%s' (help)\n", cmd.c_str());
}

void pollSerial() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      handleCommand(line);
      line = "";
    } else if (line.length() < 64) {
      line += c;
    }
  }
}

}  // namespace

void setup() {
  // Motor pins first so the driver stays disabled during boot.
  settings::load();
  bool driver_ok = motor::begin();

  Serial.begin(115200);
  delay(100);
  Serial.println();
  Serial.printf("=== Panda Bamboo Feeder custom firmware v%s ===\n", PBF_FW_VERSION);
  Serial.printf("driver: %s\n", driver_ok ? "TMC2209 OK" : "not answering (24V off?)");
  settings::print(Serial);
  printHelp();

  io::begin();
  ui::begin();
  logbuf::logf("boot: firmware v%s, driver %s", PBF_FW_VERSION, driver_ok ? "OK" : "not answering");
  stats::begin();
  feeder::begin();
  net::begin();
  moonraker::begin();
  if (settings::get().buzzer) io::beep(1, 60, 0);
}

void loop() {
  pollSerial();
  io::update();
  motor::update();
  feeder::update();
  net::update();
  stats::tick();
  delay(1);
}
