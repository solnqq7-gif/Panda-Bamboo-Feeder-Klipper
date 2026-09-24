# Third-party software

The source code in this repository is MIT licensed (see `LICENSE`). The firmware is built with
the following third-party components, which PlatformIO downloads at build time. They are not
copied into this repository, but the compiled firmware binaries contain them.

| Component | Used for | License |
|---|---|---|
| [Arduino core for ESP32](https://github.com/espressif/arduino-esp32) (espressif32 platform 7.0.0) | Framework, WiFi, web server, OTA, NVS | LGPL-2.1 |
| [ESP-IDF](https://github.com/espressif/esp-idf) (bundled with the Arduino core) | SDK, bootloader | Apache-2.0 |
| [U8g2](https://github.com/olikraus/u8g2) by olikraus | OLED display and fonts | BSD-2-Clause |
| [TMCStepper](https://github.com/teemuatlut/TMCStepper) by teemuatlut | TMC2209 stepper driver | MIT |
| [ArduinoJson](https://github.com/bblanchon/ArduinoJson) by Benoit Blanchon | JSON for the web API and Moonraker | MIT |

Because the Arduino core is LGPL-2.1, you can rebuild and relink the firmware from this
repository's complete source with PlatformIO (`pio run -e pbf_custom` in `firmware/pbf-custom`).

"Panda Bamboo Feeder" and "BIQU" are names of BIQU / BigTreeTech. This project is not affiliated
with or endorsed by them.
