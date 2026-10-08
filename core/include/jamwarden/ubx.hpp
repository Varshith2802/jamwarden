// JamWarden - streaming UBX (u-blox binary protocol) frame parser.
// Portable C++17, no heap allocation, usable on ESP32/STM32 and on a PC.
#pragma once

#include <cstddef>
#include <cstdint>

namespace jw {

constexpr uint8_t kSync1 = 0xB5;
constexpr uint8_t kSync2 = 0x62;
constexpr uint16_t kMaxPayload = 1024;  // NAV-SAT with 84 satellites = 1016 bytes

namespace cls {
constexpr uint8_t NAV = 0x01;
constexpr uint8_t ACK = 0x05;
constexpr uint8_t CFG = 0x06;
constexpr uint8_t MON = 0x0A;
}  // namespace cls

namespace id {
constexpr uint8_t NAV_STATUS = 0x03;
constexpr uint8_t NAV_PVT = 0x07;
constexpr uint8_t NAV_SAT = 0x35;
constexpr uint8_t NAV_EOE = 0x61;
constexpr uint8_t MON_HW = 0x09;
constexpr uint8_t MON_VER = 0x04;
constexpr uint8_t MON_RF = 0x38;
constexpr uint8_t CFG_MSG = 0x01;
constexpr uint8_t CFG_PRT = 0x00;
constexpr uint8_t CFG_VALSET = 0x8A;
}  // namespace id

struct UbxFrame {
  uint8_t cls = 0;
  uint8_t id = 0;
  uint16_t len = 0;
  const uint8_t* payload = nullptr;
};

class UbxParser {
 public:
  typedef void (*Callback)(const UbxFrame& frame, void* ctx);

  void set_callback(Callback cb, void* ctx) {
    cb_ = cb;
    ctx_ = ctx;
  }
  void feed(uint8_t b);
  void feed(const uint8_t* data, size_t n) {
    for (size_t i = 0; i < n; ++i) feed(data[i]);
  }
  uint32_t frames() const { return frames_; }
  uint32_t checksum_errors() const { return checksum_errors_; }
  uint32_t oversize() const { return oversize_; }

 private:
  enum State : uint8_t { kS1, kS2, kCls, kId, kLen1, kLen2, kPayload, kCkA, kCkB };
  void ck(uint8_t b) {
    cka_ = static_cast<uint8_t>(cka_ + b);
    ckb_ = static_cast<uint8_t>(ckb_ + cka_);
  }
  State st_ = kS1;
  uint8_t cls_ = 0, id_ = 0, cka_ = 0, ckb_ = 0, rx_cka_ = 0;
  uint16_t len_ = 0, idx_ = 0;
  uint8_t buf_[kMaxPayload] = {};
  Callback cb_ = nullptr;
  void* ctx_ = nullptr;
  uint32_t frames_ = 0, checksum_errors_ = 0, oversize_ = 0;
};

// Writes a complete frame (sync, header, payload, checksum) into out. Returns bytes written or 0 if cap too small.
size_t ubx_encode(uint8_t cls, uint8_t id, const uint8_t* payload, uint16_t len, uint8_t* out, size_t cap);

// Little-endian readers used by the decoders.
inline uint16_t rd_u16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
inline int16_t rd_i16(const uint8_t* p) { return static_cast<int16_t>(rd_u16(p)); }
inline uint32_t rd_u32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}
inline int32_t rd_i32(const uint8_t* p) { return static_cast<int32_t>(rd_u32(p)); }

}  // namespace jw
