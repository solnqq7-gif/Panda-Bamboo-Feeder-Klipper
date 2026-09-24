// WiFi (station, with an access-point fallback), web UI / JSON API, and OTA updates.
#pragma once

#include <Arduino.h>

namespace net {

void begin();
void update();  // call from loop()

String ipString();     // "" when there is no network
String shortStatus();  // one line for the OLED

}  // namespace net
