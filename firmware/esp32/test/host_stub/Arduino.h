// Minimal Arduino API stub so the firmware can be syntax-checked/compiled on a PC (no hardware needed).
#pragma once
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#define HIGH 1
#define LOW 0
#define OUTPUT 1
#define SERIAL_8N1 0x800001c
inline uint32_t millis() { static uint32_t t = 0; return t += 10; }
inline void delay(uint32_t) {}
inline void pinMode(int, int) {}
inline void digitalWrite(int, int) {}
class String {
 public:
  String(const char* s = "") : s_(s) {}
  String& operator+=(const char* s) { s_ += s; return *this; }
  const char* c_str() const { return s_.c_str(); }
 private:
  std::string s_;
};
class HardwareSerial {
 public:
  void begin(unsigned long, uint32_t = SERIAL_8N1, int = -1, int = -1) {}
  void end() {}
  void setRxBufferSize(size_t) {}
  int available() { return 0; }
  int read() { return -1; }
  size_t write(const uint8_t*, size_t n) { return n; }
  void flush() {}
  void println(const String& s) { std::puts(s.c_str()); }
  void println(const char* s) { std::puts(s); }
  int printf(const char* fmt, ...) { va_list a; va_start(a, fmt); int r = std::vprintf(fmt, a); va_end(a); return r; }
};
extern HardwareSerial Serial, Serial2;
