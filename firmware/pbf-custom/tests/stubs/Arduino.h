#pragma once
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <type_traits>

class String {
 public:
  std::string value;
  String() = default;
  String(const char *s) : value(s) {}
  String(std::string s) : value(s) {}
  template <class T, typename std::enable_if<std::is_integral<T>::value, int>::type = 0>
  String(T n) : value(std::to_string(n)) {}
  String(double n, int digits) {
    char text[32];
    snprintf(text, sizeof(text), "%.*f", digits, n);
    value = text;
  }
  const char *c_str() const { return value.c_str(); }
  size_t length() const { return value.length(); }
  bool isEmpty() const { return value.empty(); }
  char operator[](size_t n) const { return value[n]; }
  String &operator+=(char c) { value += c; return *this; }
  bool operator==(const String &s) const { return value == s.value; }
  bool operator!=(const String &s) const { return value != s.value; }
  friend String operator+(const String &a, const String &b) { return a.value + b.value; }
  void remove(size_t n) { value.erase(n); }
  void trim() {
    auto first = value.find_first_not_of(" \r\n\t");
    if (first == std::string::npos) { value.clear(); return; }
    value = value.substr(first, value.find_last_not_of(" \r\n\t") - first + 1);
  }
  bool equalsIgnoreCase(const String &s) const {
    auto a = value, b = s.value;
    auto lower = [](unsigned char c) { return (char)std::tolower(c); };
    std::transform(a.begin(), a.end(), a.begin(), lower);
    std::transform(b.begin(), b.end(), b.begin(), lower);
    return a == b;
  }
};

class Print {};
inline Print Serial;
inline uint32_t test_now = 1000;
inline uint32_t millis() { return test_now; }
