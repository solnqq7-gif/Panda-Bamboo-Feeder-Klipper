#include "logbuf.h"

#include <stdarg.h>

namespace logbuf {
namespace {

constexpr size_t kLines = 200;
constexpr size_t kLineLen = 128;
char ring[kLines][kLineLen];
uint32_t next_seq = 0;  // sequence number of the next line to be written
// The Moonraker task logs from the other core. Without this, two lines written at the same moment
// took the same slot: one was lost and the next slot stayed empty.
portMUX_TYPE ring_mux = portMUX_INITIALIZER_UNLOCKED;

}  // namespace

void line(const char *text) {
  char buf[kLineLen];
  snprintf(buf, sizeof(buf), "[%9.3f] %s", millis() / 1000.0f, text);
  size_t len = strlen(buf) + 1;
  portENTER_CRITICAL(&ring_mux);
  memcpy(ring[next_seq % kLines], buf, len);
  next_seq++;
  portEXIT_CRITICAL(&ring_mux);
  Serial.println(buf);
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
  portENTER_CRITICAL(&ring_mux);
  uint32_t end = next_seq;
  portEXIT_CRITICAL(&ring_mux);
  uint32_t oldest = end > kLines ? end - kLines : 0;
  if (since < oldest) since = oldest;
  if (end - since > max_lines) since = end - max_lines;
  out += '[';
  char buf[kLineLen];
  for (uint32_t seq = since; seq < end; seq++) {
    // Copy under the lock (a writer may be reusing the oldest slot), escape outside it.
    portENTER_CRITICAL(&ring_mux);
    memcpy(buf, ring[seq % kLines], kLineLen);
    portEXIT_CRITICAL(&ring_mux);
    buf[kLineLen - 1] = 0;
    if (seq != since) out += ',';
    appendJsonString(out, buf);
  }
  out += ']';
  return end;
}

}  // namespace logbuf
