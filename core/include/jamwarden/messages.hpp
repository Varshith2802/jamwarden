// Decoders for the UBX messages JamWarden uses (u-blox M8 / M9 / M10 interface descriptions).
#pragma once

#include <cstdint>

#include "jamwarden/ubx.hpp"

namespace jw {

constexpr int kMaxSats = 84;

struct NavPvt {
  uint32_t iTOW = 0;
  uint16_t year = 0;
  uint8_t month = 0, day = 0, hour = 0, min = 0, sec = 0;
  uint8_t valid = 0;  // bit0 validDate, bit1 validTime, bit2 fullyResolved
  uint32_t tAcc_ns = 0;
  int32_t nano = 0;
  uint8_t fixType = 0;  // 0 none, 2 2D, 3 3D, ...
  uint8_t flags = 0;    // bit0 gnssFixOK
  uint8_t numSV = 0;
  double lat_deg = 0, lon_deg = 0;
  double height_m = 0, hmsl_m = 0;
  double hAcc_m = 0, vAcc_m = 0;
  double velN = 0, velE = 0, velD = 0, gSpeed = 0;  // m/s
  bool gnss_fix_ok() const { return (flags & 0x01) != 0; }
  bool time_valid() const { return (valid & 0x03) == 0x03; }
};

struct SatInfo {
  uint8_t gnssId = 0, svId = 0, cno = 0;  // cno in dB-Hz
  int8_t elev = 0;                          // degrees, -91 if unknown
  int16_t azim = 0;
  bool used = false;
};

struct NavSat {
  uint32_t iTOW = 0;
  uint8_t numSvs = 0;
  SatInfo sv[kMaxSats];
};

struct NavStatus {
  uint32_t iTOW = 0;
  uint8_t gpsFix = 0;
  uint8_t flags = 0;
  uint8_t spoofDetState = 0;  // 0 unknown/off, 1 no spoofing, 2 spoofing indicated, 3 multiple indications
};

struct RfInfo {
  uint8_t jammingState = 0;  // 0 unknown, 1 ok, 2 warning, 3 critical
  uint16_t agcCnt = 0;       // 0..8191
  uint16_t noisePerMS = 0;
  uint8_t jamInd = 0;        // 0..255 CW jamming indicator
  bool from_mon_hw = false;
};

bool decode_nav_pvt(const UbxFrame& f, NavPvt& out);
bool decode_nav_sat(const UbxFrame& f, NavSat& out);
bool decode_nav_status(const UbxFrame& f, NavStatus& out);
bool decode_mon_rf(const UbxFrame& f, RfInfo& out);  // first RF block (L1)
bool decode_mon_hw(const UbxFrame& f, RfInfo& out);  // M8 fallback

// UTC time of the solution in milliseconds since 1970, or -1 if date/time are not valid.
int64_t pvt_unix_ms(const NavPvt& p);

}  // namespace jw
