// Log lines go to the serial port and to a ring buffer that the web page polls.
#pragma once

#include <Arduino.h>

namespace logbuf {

void logf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void line(const char *text);  // adds the timestamp prefix
// Appends lines with sequence number >= since as a JSON array to out, returns the next sequence.
uint32_t appendJsonSince(uint32_t since, String &out, size_t max_lines = 80);

// JSON string escaping, shared with the web API.
void appendJsonString(String &out, const char *s);

}  // namespace logbuf
