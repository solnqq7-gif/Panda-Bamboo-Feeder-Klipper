#include "stats.h"

#include <Preferences.h>

#include "logbuf.h"

namespace stats {
namespace {

constexpr uint32_t kMagic = 0x50424653;  // "PBFS"
constexpr uint16_t kLayout = 1;          // bump when the fields below change
constexpr uint32_t kFlushMs = 600000;    // 10 minutes: NVS cells wear out, a print does >500 top-ups
constexpr const char *kNamespace = "pbfstat";
constexpr const char *kKey = "blob";

struct __attribute__((packed)) Data {
  uint32_t magic;
  uint16_t layout;
  uint16_t fw_hash;
  uint32_t fed_mm;       // pushed towards the printer
  uint32_t pulled_mm;    // pulled back towards the box
  uint32_t top_ups;
  uint32_t uart_hiccups;
  uint16_t stuck_alarms;
  uint16_t unload_jams;
  uint32_t powered_min;
  uint32_t checksum;
};

Data data{};
bool dirty = false;
bool usable = false;       // false = something was wrong, counters are RAM only
uint32_t last_flush_ms = 0;
uint32_t last_minute_ms = 0;
uint32_t uart_base = 0;    // hiccups already counted, so a reconnecting counter cannot double count
float fed_frac = 0;        // sub-millimetre remainders
float pulled_frac = 0;

uint16_t fwHash() {
  uint16_t h = 0;
  for (const char *p = PBF_FW_VERSION; *p; ++p) h = (uint16_t)(h * 31 + (uint8_t)*p);
  return h;
}

uint32_t checksum(const Data &d) {
  const uint8_t *p = (const uint8_t *)&d;
  size_t n = offsetof(Data, checksum);
  uint32_t sum = 0x1234abcd;
  for (size_t i = 0; i < n; ++i) sum = (sum << 3) ^ (sum >> 29) ^ p[i];
  return sum;
}

void reset(const char *why) {
  uint16_t fw = fwHash();
  data = Data{};
  data.magic = kMagic;
  data.layout = kLayout;
  data.fw_hash = fw;
  dirty = true;
  if (why) logbuf::logf("stats: starting from zero (%s)", why);
}

void save() {
  if (!usable) return;
  data.checksum = checksum(data);
  Preferences prefs;
  if (!prefs.begin(kNamespace, false)) {
    usable = false;  // cannot store them: keep counting in RAM, never retry in a loop
    logbuf::line("stats: flash not writable, counters are session only");
    return;
  }
  prefs.putBytes(kKey, &data, sizeof(data));
  prefs.end();
  dirty = false;
  last_flush_ms = millis();
}

}  // namespace

void begin() {
  usable = true;
  last_flush_ms = millis();
  last_minute_ms = millis();
  Preferences prefs;
  if (!prefs.begin(kNamespace, true)) {
    reset("no stored counters yet");
    return;
  }
  Data stored{};
  size_t n = prefs.getBytesLength(kKey);
  bool read = n == sizeof(stored) && prefs.getBytes(kKey, &stored, sizeof(stored)) == sizeof(stored);
  prefs.end();
  if (!read) {
    reset("no stored counters yet");
  } else if (stored.magic != kMagic || stored.layout != kLayout) {
    reset("stored counters have a different layout");
  } else if (stored.checksum != checksum(stored)) {
    reset("stored counters failed their checksum");
  } else if (stored.fw_hash != fwHash()) {
    reset("firmware changed");
  } else {
    data = stored;
    dirty = false;
  }
}

void tick() {
  uint32_t now = millis();
  if (now - last_minute_ms >= 60000) {
    last_minute_ms += 60000;
    data.powered_min++;
    dirty = true;
  }
  if (dirty && now - last_flush_ms >= kFlushMs) save();
}

void flush() {
  if (dirty) save();
}

void clear() {
  reset("cleared by the user");
  Preferences prefs;
  if (prefs.begin(kNamespace, false)) {
    prefs.remove(kKey);
    prefs.end();
  }
  usable = true;
  save();
}

void addMoved(float mm) {
  if (mm > 0) {
    fed_frac += mm;
    if (fed_frac >= 1.0f) {
      uint32_t whole = (uint32_t)fed_frac;
      data.fed_mm += whole;
      fed_frac -= whole;
      dirty = true;
    }
  } else if (mm < 0) {
    pulled_frac -= mm;
    if (pulled_frac >= 1.0f) {
      uint32_t whole = (uint32_t)pulled_frac;
      data.pulled_mm += whole;
      pulled_frac -= whole;
      dirty = true;
    }
  }
}

void addTopUps(uint16_t n, float mm) {
  if (!n) return;
  data.top_ups += n;
  dirty = true;
  (void)mm;  // the distance is already counted by addMoved
}

void addStuck() {
  data.stuck_alarms++;
  dirty = true;
}

void addUnloadJam() {
  data.unload_jams++;
  dirty = true;
}

void setUartHiccups(uint32_t total_since_boot) {
  if (total_since_boot < uart_base) uart_base = 0;  // counter restarted
  uint32_t add = total_since_boot - uart_base;
  if (!add) return;
  uart_base = total_since_boot;
  data.uart_hiccups += add;
  dirty = true;
}

void appendJson(String &out) {
  out += "{\"stored\":" + String(usable ? "true" : "false");
  out += ",\"fed_m\":" + String(data.fed_mm / 1000.0f, 2);
  out += ",\"pulled_m\":" + String(data.pulled_mm / 1000.0f, 2);
  out += ",\"top_ups\":" + String(data.top_ups);
  out += ",\"uart_hiccups\":" + String(data.uart_hiccups);
  out += ",\"stuck_alarms\":" + String(data.stuck_alarms);
  out += ",\"unload_jams\":" + String(data.unload_jams);
  out += ",\"powered_h\":" + String(data.powered_min / 60.0f, 1);
  out += '}';
}

}  // namespace stats
