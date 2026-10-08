#include "jamwarden/ubx.hpp"

namespace jw {

void UbxParser::feed(uint8_t b) {
  switch (st_) {
    case kS1:
      if (b == kSync1) st_ = kS2;
      break;
    case kS2:
      st_ = (b == kSync2) ? kCls : (b == kSync1 ? kS2 : kS1);
      break;
    case kCls:
      cls_ = b;
      cka_ = ckb_ = 0;
      ck(b);
      st_ = kId;
      break;
    case kId:
      id_ = b;
      ck(b);
      st_ = kLen1;
      break;
    case kLen1:
      len_ = b;
      ck(b);
      st_ = kLen2;
      break;
    case kLen2:
      len_ = static_cast<uint16_t>(len_ | (static_cast<uint16_t>(b) << 8));
      ck(b);
      idx_ = 0;
      if (len_ > kMaxPayload) {
        ++oversize_;
        st_ = kS1;
      } else {
        st_ = len_ ? kPayload : kCkA;
      }
      break;
    case kPayload:
      buf_[idx_++] = b;
      ck(b);
      if (idx_ >= len_) st_ = kCkA;
      break;
    case kCkA:
      rx_cka_ = b;
      st_ = kCkB;
      break;
    case kCkB:
      if (rx_cka_ == cka_ && b == ckb_) {
        ++frames_;
        if (cb_) {
          UbxFrame f;
          f.cls = cls_;
          f.id = id_;
          f.len = len_;
          f.payload = buf_;
          cb_(f, ctx_);
        }
      } else {
        ++checksum_errors_;
      }
      st_ = kS1;
      break;
  }
}

size_t ubx_encode(uint8_t cls, uint8_t id, const uint8_t* payload, uint16_t len, uint8_t* out, size_t cap) {
  const size_t total = static_cast<size_t>(len) + 8;
  if (cap < total) return 0;
  out[0] = kSync1;
  out[1] = kSync2;
  out[2] = cls;
  out[3] = id;
  out[4] = static_cast<uint8_t>(len & 0xFF);
  out[5] = static_cast<uint8_t>(len >> 8);
  for (uint16_t i = 0; i < len; ++i) out[6 + i] = payload[i];
  uint8_t a = 0, b = 0;
  for (size_t i = 2; i < 6 + static_cast<size_t>(len); ++i) {
    a = static_cast<uint8_t>(a + out[i]);
    b = static_cast<uint8_t>(b + a);
  }
  out[6 + len] = a;
  out[7 + len] = b;
  return total;
}

}  // namespace jw
