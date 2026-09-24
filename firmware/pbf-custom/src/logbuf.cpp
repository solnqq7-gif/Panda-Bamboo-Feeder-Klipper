#include "logbuf.h"

#include <stdarg.h>

namespace logbuf {
namespace {

constexpr size_t kLines = 200;
constexpr size_t kLineLen = 128;
char ring[kLines][kLineLen];
uint32_t next_seq = 0;  // sequence number of the next line to be written

}  // namespace

void line(const char *text) {
  char *slot = ring[next_seq % kLines];
  snprintf(slot, kLineLen, "[%9.3f] %s", millis() / 1000.0f, text);
  next_seq++;
  Serial.println(slot);
}

void logf(const char *fmt, ...) {
  char buf[kLineLen];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  line(buf);
}

void appendJsonString(String &out, const char *s) {
  out += '"';
  for (; *s; s++) {
    char c = *s;
    if (c == '"' || c == '\\') {
      out += '\\';
      out += c;
    } else if ((uint8_t)c < 0x20) {
      out += ' ';
    } else {
      out += c;
    }
  }
  out += '"';
}

uint32_t appendJsonSince(uint32_t since, String &out, size_t max_lines) {
  uint32_t oldest = next_seq > kLines ? next_seq - kLines : 0;
  if (since < oldest) since = oldest;
  if (next_seq - since > max_lines) since = next_seq - max_lines;
  out += '[';
  for (uint32_t seq = since; seq < next_seq; seq++) {
    if (seq != since) out += ',';
    appendJsonString(out, ring[seq % kLines]);
  }
  out += ']';
  return next_seq;
}

}  // namespace logbuf
