// Hardware constants. Pin numbers were verified on the device.
#pragma once

#include <stdint.h>

namespace pins {
constexpr int kEn = 20;  // TMC2209 ENN, LOW = driver enabled
constexpr int kStep = 9;
constexpr int kDir = 10;
constexpr int kUartTx = 11;
constexpr int kUartRx = 12;
constexpr int kKeyUp = 5;    // "up / load" key, LOW = pressed
constexpr int kKeyBack = 2;  // "back / unload" key, LOW = pressed
constexpr int kIn1 = 14;     // first inlet sensor, HIGH = filament present
constexpr int kIn2 = 21;     // second inlet sensor at the drive gear, HIGH = filament present
constexpr int kBufEnd = 47;  // buffer carriage at its inner end, HIGH = at rest
constexpr int kBufPos = 48;  // comb along the carriage travel (~3 mm pitch)
constexpr int kBuzzer = 1;   // LEDC output in the stock firmware
constexpr int kOledSclk = 19;
constexpr int kOledMosi = 8;
constexpr int kOledCs = 17;
constexpr int kOledDc = 18;
}  // namespace pins

namespace hw {
constexpr float kRSense = 0.11f;
constexpr uint8_t kDriverAddr = 0;
constexpr uint32_t kUartBaud = 38400;
constexpr uint16_t kMicrosteps = 32;
constexpr uint8_t kOledXOffset = 2;   // 132-column controller, visible area starts at column 2
constexpr float kBufferTravelMm = 21.4f;  // carriage travel, from the official CAD

// Pressure level 1..10 -> motor run current. Stock firmware runs at 600 mA.
constexpr uint8_t kMinPressureLevel = 1;
constexpr uint8_t kMaxPressureLevel = 10;
inline uint16_t pressureLevelToMa(uint8_t level) { return 250 + (level - 1) * 50; }
}  // namespace hw
