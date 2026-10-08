#include "jamwarden/messages.hpp"

namespace jw {

bool decode_nav_pvt(const UbxFrame& f, NavPvt& o) {
  if (f.cls != cls::NAV || f.id != id::NAV_PVT || f.len < 92) return false;
  const uint8_t* p = f.payload;
  o.iTOW = rd_u32(p + 0);
  o.year = rd_u16(p + 4);
  o.month = p[6];
  o.day = p[7];
  o.hour = p[8];
  o.min = p[9];
  o.sec = p[10];
  o.valid = p[11];
  o.tAcc_ns = rd_u32(p + 12);
  o.nano = rd_i32(p + 16);
  o.fixType = p[20];
  o.flags = p[21];
  o.numSV = p[23];
  o.lon_deg = rd_i32(p + 24) * 1e-7;
  o.lat_deg = rd_i32(p + 28) * 1e-7;
  o.height_m = rd_i32(p + 32) * 1e-3;
  o.hmsl_m = rd_i32(p + 36) * 1e-3;
  o.hAcc_m = rd_u32(p + 40) * 1e-3;
  o.vAcc_m = rd_u32(p + 44) * 1e-3;
  o.velN = rd_i32(p + 48) * 1e-3;
  o.velE = rd_i32(p + 52) * 1e-3;
  o.velD = rd_i32(p + 56) * 1e-3;
  o.gSpeed = rd_i32(p + 60) * 1e-3;
  return true;
}

bool decode_nav_sat(const UbxFrame& f, NavSat& o) {
  if (f.cls != cls::NAV || f.id != id::NAV_SAT || f.len < 8) return false;
  const uint8_t* p = f.payload;
  o.iTOW = rd_u32(p);
  uint8_t n = p[5];
  if (f.len < 8 + 12 * static_cast<int>(n)) return false;
  if (n > kMaxSats) n = kMaxSats;
  o.numSvs = n;
  for (int i = 0; i < n; ++i) {
    const uint8_t* s = p + 8 + 12 * i;
    SatInfo& sv = o.sv[i];
    sv.gnssId = s[0];
    sv.svId = s[1];
    sv.cno = s[2];
    sv.elev = static_cast<int8_t>(s[3]);
    sv.azim = rd_i16(s + 4);
    sv.used = (rd_u32(s + 8) & 0x08) != 0;
  }
  return true;
}

bool decode_nav_status(const UbxFrame& f, NavStatus& o) {
  if (f.cls != cls::NAV || f.id != id::NAV_STATUS || f.len < 16) return false;
  o.iTOW = rd_u32(f.payload);
  o.gpsFix = f.payload[4];
  o.flags = f.payload[5];
  o.spoofDetState = static_cast<uint8_t>((f.payload[7] >> 3) & 0x03);
  return true;
}

bool decode_mon_rf(const UbxFrame& f, RfInfo& o) {
  if (f.cls != cls::MON || f.id != id::MON_RF || f.len < 4 + 24) return false;
  const uint8_t* b = f.payload + 4;  // first block
  o.jammingState = static_cast<uint8_t>(b[1] & 0x03);
  o.noisePerMS = rd_u16(b + 12);
  o.agcCnt = rd_u16(b + 14);
  o.jamInd = b[16];
  o.from_mon_hw = false;
  return true;
}

bool decode_mon_hw(const UbxFrame& f, RfInfo& o) {
  if (f.cls != cls::MON || f.id != id::MON_HW || f.len < 60) return false;
  const uint8_t* p = f.payload;
  o.noisePerMS = rd_u16(p + 16);
  o.agcCnt = rd_u16(p + 18);
  o.jammingState = static_cast<uint8_t>((p[22] >> 2) & 0x03);
  o.jamInd = p[45];
  o.from_mon_hw = true;
  return true;
}

static int64_t days_from_civil(int64_t y, unsigned m, unsigned d) {  // Howard Hinnant's algorithm
  y -= m <= 2;
  const int64_t era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + static_cast<int64_t>(doe) - 719468;
}

int64_t pvt_unix_ms(const NavPvt& p) {
  if (!p.time_valid() || p.year < 2000 || p.month < 1 || p.month > 12 || p.day < 1) return -1;
  const int64_t days = days_from_civil(p.year, p.month, p.day);
  const int64_t secs = days * 86400 + p.hour * 3600 + p.min * 60 + p.sec;
  return secs * 1000 + p.nano / 1000000;
}

}  // namespace jw
