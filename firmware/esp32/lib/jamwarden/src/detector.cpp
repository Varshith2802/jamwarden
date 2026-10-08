#include "jamwarden/detector.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>

namespace jw {

namespace {
constexpr double kPi = 3.14159265358979323846;

const uint32_t kRfReasons = R_AGC_ANOMALY | R_NOISE_RISE | R_JAMIND_HIGH | R_UBX_JAM_WARN | R_UBX_JAM_CRIT;
const uint32_t kEffectReasons = R_CNO_DROP | R_SATS_LOST | R_FIX_LOST;
const uint32_t kKinematicReasons = R_STATIC_DISPLACED | R_POS_JUMP | R_TIME_JUMP | R_ALT_JUMP | R_SPEED_MISMATCH;

double weight(uint32_t r) {
  switch (r) {
    case R_CNO_DROP: return 2.0;
    case R_AGC_ANOMALY: return 2.0;
    case R_NOISE_RISE: return 1.5;
    case R_JAMIND_HIGH: return 1.5;
    case R_UBX_JAM_WARN: return 1.5;
    case R_UBX_JAM_CRIT: return 3.0;
    case R_FIX_LOST: return 1.0;
    case R_SATS_LOST: return 1.0;
    case R_UBX_SPOOF: return 3.0;
    case R_POS_JUMP: return 3.0;
    case R_STATIC_DISPLACED: return 3.0;
    case R_SPEED_MISMATCH: return 2.0;
    case R_CNO_UNIFORM: return 1.5;
    case R_ELEV_CORR_LOW: return 1.5;
    case R_POWER_RISE: return 1.0;
    case R_TIME_JUMP: return 3.0;
    case R_ALT_JUMP: return 2.0;
    default: return 0.0;
  }
}

double score(uint32_t mask) {
  double s = 0;
  for (int i = 0; i < 32; ++i)
    if (mask & (1u << i)) s += weight(1u << i);
  return s;
}

void civil_from_days(int64_t z, int& y, unsigned& m, unsigned& d) {
  z += 719468;
  const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned doe = static_cast<unsigned>(z - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const int64_t yy = static_cast<int64_t>(yoe) + era * 400;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  d = doy - (153 * mp + 2) / 5 + 1;
  m = mp < 10 ? mp + 3 : mp - 9;
  y = static_cast<int>(yy + (m <= 2));
}
}  // namespace

const char* reason_name(uint32_t bit) {
  switch (bit) {
    case R_CNO_DROP: return "cno_drop";
    case R_AGC_ANOMALY: return "agc_anomaly";
    case R_NOISE_RISE: return "noise_rise";
    case R_JAMIND_HIGH: return "cw_jamming_indicator";
    case R_UBX_JAM_WARN: return "receiver_jamming_warning";
    case R_UBX_JAM_CRIT: return "receiver_jamming_critical";
    case R_FIX_LOST: return "fix_lost";
    case R_SATS_LOST: return "satellites_lost";
    case R_UBX_SPOOF: return "receiver_spoofing_flag";
    case R_POS_JUMP: return "position_jump";
    case R_STATIC_DISPLACED: return "static_position_displaced";
    case R_SPEED_MISMATCH: return "speed_mismatch";
    case R_CNO_UNIFORM: return "cno_uniform";
    case R_ELEV_CORR_LOW: return "no_elevation_dependence";
    case R_POWER_RISE: return "signal_power_rise";
    case R_TIME_JUMP: return "time_jump";
    case R_ALT_JUMP: return "altitude_jump";
    default: return "unknown";
  }
}

std::string reasons_json(uint32_t mask) {
  std::string s = "[";
  bool first = true;
  for (int i = 0; i < 32; ++i) {
    if (mask & (1u << i)) {
      if (!first) s += ",";
      s += "\"";
      s += reason_name(1u << i);
      s += "\"";
      first = false;
    }
  }
  return s + "]";
}

const char* event_name(EventKind k) {
  switch (k) {
    case EventKind::BaselineReady: return "baseline_ready";
    case EventKind::JammingStart: return "jamming_start";
    case EventKind::JammingEnd: return "jamming_end";
    case EventKind::SpoofingStart: return "spoofing_start";
    case EventKind::SpoofingEnd: return "spoofing_end";
  }
  return "unknown";
}

double haversine_m(double lat1, double lon1, double lat2, double lon2) {
  const double r = 6371008.8;
  const double p1 = lat1 * kPi / 180, p2 = lat2 * kPi / 180;
  const double dp = (lat2 - lat1) * kPi / 180, dl = (lon2 - lon1) * kPi / 180;
  const double a = std::sin(dp / 2) * std::sin(dp / 2) + std::cos(p1) * std::cos(p2) * std::sin(dl / 2) * std::sin(dl / 2);
  return 2 * r * std::atan2(std::sqrt(a), std::sqrt(1 - a));
}

std::string iso_utc(int64_t ms) {
  if (ms < 0) return "null";
  int64_t secs = ms / 1000;
  const int msec = static_cast<int>(ms % 1000);
  int64_t days = secs / 86400;
  int64_t rem = secs % 86400;
  int y;
  unsigned m, d;
  civil_from_days(days, y, m, d);
  char buf[40];
  std::snprintf(buf, sizeof(buf), "\"%04d-%02u-%02uT%02d:%02d:%02d.%03dZ\"", y, m, d, static_cast<int>(rem / 3600),
                static_cast<int>((rem % 3600) / 60), static_cast<int>(rem % 60), msec);
  return buf;
}

// ------------------------------------------------------------------------------------------- Stat
void Detector::Stat::learn(double x) {
  ++n;
  const double d = x - mean;
  mean += d / n;
  m2 += d * (x - mean);
}
void Detector::Stat::finish() { var = n > 1 ? m2 / (n - 1) : 0.0; }
void Detector::Stat::adapt(double x, double a) {
  const double d = x - mean;
  mean += a * d;
  var = (1 - a) * (var + a * d * d);
}
double Detector::Stat::sd() const { return std::sqrt(std::max(var, 0.0)); }

// ------------------------------------------------------------------------------------------- Detector
Detector::Detector(const DetectorConfig& cfg) : cfg_(cfg) {}

void Detector::set_reference(double lat, double lon, double height) {
  ref_lat_ = lat;
  ref_lon_ = lon;
  ref_h_ = height;
  ref_n_ = 1;
  ref_fixed_ = true;
}

void Detector::on_frame(const UbxFrame& f, uint32_t local_ms) {
  if (f.cls == cls::NAV) {
    if (f.id == id::NAV_PVT && decode_nav_pvt(f, pvt_)) {
      have_pvt_ = true;
      if (!seen_eoe_) process_epoch(local_ms);
    } else if (f.id == id::NAV_SAT && decode_nav_sat(f, sat_)) {
      have_sat_ = true;
    } else if (f.id == id::NAV_STATUS && decode_nav_status(f, status_)) {
      have_status_ = true;
    } else if (f.id == id::NAV_EOE && f.len >= 4) {
      seen_eoe_ = true;
      if (have_pvt_ && rd_u32(f.payload) == pvt_.iTOW) process_epoch(local_ms);
    }
  } else if (f.cls == cls::MON) {
    RfInfo rf;
    if ((f.id == id::MON_RF && decode_mon_rf(f, rf)) || (f.id == id::MON_HW && decode_mon_hw(f, rf))) {
      // Prefer MON-RF when a receiver sends both.
      if (!(rf.from_mon_hw && have_rf_ && !rf_.from_mon_hw)) {
        rf_ = rf;
        have_rf_ = true;
        rf_local_ms_ = local_ms;
      }
    }
  }
}

bool Detector::epoch_ready() {
  const bool r = ready_flag_;
  ready_flag_ = false;
  return r;
}

void Detector::push_event(EventKind k, const char* sev, uint32_t reasons) {
  Event e;
  e.kind = k;
  e.severity = sev;
  e.reasons = reasons;
  e.m = m_;
  const int idx = (q_head_ + q_len_) % kQ;
  q_[idx] = e;
  if (q_len_ < kQ) {
    ++q_len_;
  } else {
    q_head_ = (q_head_ + 1) % kQ;  // drop oldest
  }
}

bool Detector::poll_event(Event& out) {
  if (q_len_ == 0) return false;
  out = q_[q_head_];
  q_head_ = (q_head_ + 1) % kQ;
  --q_len_;
  return true;
}

void Detector::compute_signal_features() {
  // Per-satellite statistics from NAV-SAT of the same epoch.
  double cnos[kMaxSats];
  int n = 0, strong = 0;
  double sx = 0, sy = 0, sxx = 0, syy = 0, sxy = 0;
  int np = 0;
  double hi_sum = 0, hi_sq = 0;
  int hi_n = 0;
  const bool sat_fresh = have_sat_ && (sat_.iTOW == pvt_.iTOW || (pvt_.iTOW >= sat_.iTOW && pvt_.iTOW - sat_.iTOW <= 1000));
  if (sat_fresh) {
    for (int i = 0; i < sat_.numSvs; ++i) {
      const SatInfo& s = sat_.sv[i];
      if (s.cno == 0) continue;
      cnos[n++] = s.cno;
      if (s.cno >= 30) ++strong;
      if (s.elev >= 5 && s.elev <= 90) {
        const double x = s.elev, y = s.cno;
        sx += x;
        sy += y;
        sxx += x * x;
        syy += y * y;
        sxy += x * y;
        ++np;
      }
      if (s.elev >= 15 && s.elev <= 90) {
        hi_sum += s.cno;
        hi_sq += static_cast<double>(s.cno) * s.cno;
        ++hi_n;
      }
    }
  }
  m_.tracked = static_cast<uint8_t>(n);
  m_.strong = static_cast<uint8_t>(strong);
  std::sort(cnos, cnos + n, [](double a, double b) { return a > b; });
  const int top = std::min(n, 6);
  double t = 0;
  for (int i = 0; i < top; ++i) t += cnos[i];
  m_.cno_top = top ? t / top : 0.0;

  epoch_struct_valid_ = false;
  if (np >= 6 && hi_n >= 6) {
    const double cov = sxy / np - (sx / np) * (sy / np);
    const double vx = sxx / np - (sx / np) * (sx / np);
    const double vy = syy / np - (sy / np) * (sy / np);
    epoch_corr_ = (vx > 1e-9 && vy > 1e-9) ? cov / std::sqrt(vx * vy) : 0.0;
    const double mean = hi_sum / hi_n;
    epoch_std_ = std::sqrt(std::max(0.0, hi_sq / hi_n - mean * mean));
    epoch_struct_valid_ = true;
    corr_win_[win_i_] = epoch_corr_;
    std_win_[win_i_] = epoch_std_;
    win_i_ = (win_i_ + 1) % std::min(cfg_.window, kWin);
    win_n_ = std::min(win_n_ + 1, std::min(cfg_.window, kWin));
  }
  if (win_n_ > 0) {
    double tmp[kWin];
    double ssum = 0;
    for (int i = 0; i < win_n_; ++i) {
      tmp[i] = corr_win_[i];
      ssum += std_win_[i];
    }
    std::sort(tmp, tmp + win_n_);
    m_.elev_corr = (win_n_ % 2) ? tmp[win_n_ / 2] : 0.5 * (tmp[win_n_ / 2 - 1] + tmp[win_n_ / 2]);
    m_.cno_std = ssum / win_n_;
  }
}

void Detector::process_epoch(uint32_t local_ms) {
  if (processed_any_ && pvt_.iTOW == last_processed_itow_) return;
  processed_any_ = true;
  last_processed_itow_ = pvt_.iTOW;
  ++m_.epoch;
  m_.iTOW = pvt_.iTOW;
  m_.local_ms = local_ms;
  m_.utc_ms = pvt_unix_ms(pvt_);
  m_.fixType = pvt_.fixType;
  m_.numSV = pvt_.numSV;
  const bool fix = pvt_.fixType >= 3 && pvt_.gnss_fix_ok();
  if (fix) {
    m_.lat = pvt_.lat_deg;
    m_.lon = pvt_.lon_deg;
    m_.height = pvt_.height_m;
    m_.hAcc = pvt_.hAcc_m;
    m_.gSpeed = pvt_.gSpeed;
  }
  compute_signal_features();
  const bool rf_fresh = have_rf_ && (local_ms - rf_local_ms_) <= 2500;
  m_.agc = rf_fresh ? rf_.agcCnt : -1;
  m_.noise = rf_fresh ? rf_.noisePerMS : -1;
  m_.jamInd = rf_fresh ? rf_.jamInd : -1;
  m_.ubx_jam = rf_fresh ? rf_.jammingState : 0;
  m_.ubx_spoof = (have_status_ && status_.iTOW == pvt_.iTOW) ? status_.spoofDetState : 0;

  // Time consistency: GNSS time vs the local monotonic clock.
  m_.time_jump_ms = 0;
  int64_t offset = 0;
  const bool have_time = m_.utc_ms >= 0;
  if (have_time) {
    offset = m_.utc_ms - static_cast<int64_t>(local_ms);
    if (have_prev_offset_) m_.time_jump_ms = offset - prev_offset_;
  }

  // ------------------------------------------------------------------ learning phase
  if (m_.learning) {
    if (fix && m_.tracked >= 4) {
      cno_top_.learn(m_.cno_top);
      strong_.learn(m_.strong);
      if (m_.agc >= 0) agc_.learn(m_.agc);
      if (m_.noise >= 0) noise_.learn(m_.noise);
      if (m_.jamInd >= 0) jamind_.learn(m_.jamInd);
      if (epoch_struct_valid_) {
        cno_std_.learn(epoch_std_);
        corr_.learn(epoch_corr_);
      }
      if (have_time) offset_.learn(static_cast<double>(offset));
      if (!ref_fixed_) {
        ++ref_n_;
        ref_lat_ += (m_.lat - ref_lat_) / ref_n_;
        ref_lon_ += (m_.lon - ref_lon_) / ref_n_;
        ref_h_ += (m_.height - ref_h_) / ref_n_;
      }
      ++learned_;
    }
    if (learned_ >= cfg_.baseline_epochs) {
      for (Stat* s : {&cno_top_, &strong_, &agc_, &noise_, &jamind_, &cno_std_, &corr_, &offset_}) s->finish();
      m_.learning = false;
      push_event(EventKind::BaselineReady, "info", 0);
    }
    if (fix) {
      have_prev_fix_ = true;
      prev_lat_ = m_.lat;
      prev_lon_ = m_.lon;
      prev_h_ = m_.height;
      prev_utc_ = m_.utc_ms;
      prev_local_ = local_ms;
    }
    if (have_time) {
      prev_offset_ = offset;
      have_prev_offset_ = true;
    }
    ready_flag_ = true;
    return;
  }

  // ------------------------------------------------------------------ jamming indicators
  uint32_t jam = 0;
  if (m_.tracked == 0 || cno_top_.mean - m_.cno_top >= cfg_.cno_drop_db) jam |= R_CNO_DROP;
  if (strong_.mean >= 4 && m_.strong < 0.5 * strong_.mean) jam |= R_SATS_LOST;
  if (!fix) jam |= R_FIX_LOST;
  if (m_.agc >= 0 && agc_.n > 0) {
    const double dev = std::fabs(m_.agc - agc_.mean);
    if (dev > std::max(cfg_.agc_sigma * agc_.sd(), cfg_.agc_min_rel * agc_.mean)) jam |= R_AGC_ANOMALY;
  }
  if (m_.noise >= 0 && noise_.n > 0) {
    const double rise = m_.noise - noise_.mean;
    if (rise > std::max(cfg_.noise_sigma * noise_.sd(), cfg_.noise_min_rel * noise_.mean)) jam |= R_NOISE_RISE;
  }
  if (m_.jamInd >= 0 && jamind_.n > 0 && m_.jamInd > jamind_.mean + std::max<double>(cfg_.jamind_margin, 4 * jamind_.sd()))
    jam |= R_JAMIND_HIGH;
  if (m_.ubx_jam == 2) jam |= R_UBX_JAM_WARN;
  if (m_.ubx_jam == 3) jam |= R_UBX_JAM_CRIT;

  // ------------------------------------------------------------------ spoofing indicators
  uint32_t spoof = 0;
  if (m_.ubx_spoof >= 2) spoof |= R_UBX_SPOOF;
  m_.displacement_m = 0;
  m_.implied_speed = 0;
  if (fix) {
    if (cfg_.static_mode && ref_n_ > 0) {
      m_.displacement_m = haversine_m(ref_lat_, ref_lon_, m_.lat, m_.lon);
      if (m_.displacement_m > cfg_.static_radius_m && m_.hAcc < cfg_.static_radius_m / 2) spoof |= R_STATIC_DISPLACED;
      if (std::fabs(m_.height - ref_h_) > cfg_.max_alt_jump_m && pvt_.vAcc_m < cfg_.max_alt_jump_m / 2) spoof |= R_ALT_JUMP;
    }
    if (have_prev_fix_) {
      const double dist = haversine_m(prev_lat_, prev_lon_, m_.lat, m_.lon);
      double dt = (static_cast<double>(local_ms) - prev_local_) / 1000.0;
      if (dt <= 0.05) dt = 1.0;
      m_.implied_speed = dist / dt;
      const double limit = cfg_.static_mode ? 10.0 : cfg_.max_speed_mps;
      if (m_.implied_speed > limit && dist > 3 * std::max(m_.hAcc, 2.0)) spoof |= R_POS_JUMP;
      if (!cfg_.static_mode && std::fabs(m_.implied_speed - m_.gSpeed) > std::max(10.0, 0.5 * m_.gSpeed))
        spoof |= R_SPEED_MISMATCH;
      if (!cfg_.static_mode && std::fabs(m_.height - prev_h_) > cfg_.max_alt_jump_m) spoof |= R_ALT_JUMP;
    }
  }
  if (have_time && have_prev_offset_) {
    if (std::llabs(m_.time_jump_ms) > cfg_.time_jump_ms) spoof |= R_TIME_JUMP;
    // persistent shift of GNSS time against the (drift-tracked) local clock = time spoofing
    if (offset_.n > 0 && std::fabs(static_cast<double>(offset) - offset_.mean) > static_cast<double>(cfg_.time_jump_ms))
      spoof |= R_TIME_JUMP;
  }
  if (win_n_ >= std::min(cfg_.window, kWin) / 2 && m_.tracked >= 6) {
    if (m_.cno_std < cfg_.uniform_std_db && cno_std_.mean > cfg_.uniform_std_db + 1.0) spoof |= R_CNO_UNIFORM;
    if (corr_.mean > cfg_.baseline_corr_min && m_.elev_corr < cfg_.elev_corr_min) spoof |= R_ELEV_CORR_LOW;
  }
  if (m_.tracked >= 4 && m_.cno_top - cno_top_.mean >= cfg_.power_rise_db) spoof |= R_POWER_RISE;
  // A single-epoch jump of the solution is not noise: keep it as an indicator for a few epochs.
  const uint32_t jumps = spoof & (R_POS_JUMP | R_ALT_JUMP);
  if (jumps) {
    latched_ |= jumps;
    latch_left_ = cfg_.latch_epochs;
  } else if (latch_left_ > 0) {
    spoof |= latched_;
    if (--latch_left_ == 0) latched_ = 0;
  }

  m_.jam_reasons = jam;
  m_.spoof_reasons = spoof;
  m_.jam_score = score(jam);
  m_.spoof_score = score(spoof);
  const bool ubx_crit = (jam & R_UBX_JAM_CRIT) != 0;
  const bool jam_candidate = ((jam & kRfReasons) && (jam & kEffectReasons) && m_.jam_score >= cfg_.jam_threshold) ||
                             (ubx_crit && m_.jam_score >= 4.0);
  const bool spoof_candidate = m_.spoof_score >= cfg_.spoof_threshold;

  // ------------------------------------------------------------------ hysteresis + events
  if (!m_.jamming) {
    jam_up_ = jam_candidate ? jam_up_ + 1 : 0;
    if (jam_up_ >= cfg_.raise_epochs) {
      m_.jamming = true;
      jam_down_ = 0;
      jam_episode_reasons_ = jam;
      const bool crit = (jam & R_FIX_LOST) || ubx_crit || (cno_top_.mean - m_.cno_top >= 15.0);
      push_event(EventKind::JammingStart, crit ? "critical" : "warning", jam);
    }
  } else {
    jam_episode_reasons_ |= jam;
    jam_down_ = jam_candidate ? 0 : jam_down_ + 1;
    if (jam_down_ >= cfg_.clear_epochs) {
      m_.jamming = false;
      jam_up_ = 0;
      push_event(EventKind::JammingEnd, "info", jam_episode_reasons_);
    }
  }
  if (!m_.spoofing) {
    spoof_up_ = spoof_candidate ? spoof_up_ + 1 : 0;
    if (spoof_up_ >= cfg_.raise_epochs) {
      m_.spoofing = true;
      spoof_down_ = 0;
      spoof_episode_reasons_ = spoof;
      push_event(EventKind::SpoofingStart, (spoof & kKinematicReasons) ? "critical" : "warning", spoof);
    }
  } else {
    spoof_episode_reasons_ |= spoof;
    spoof_down_ = spoof_candidate ? 0 : spoof_down_ + 1;
    if (spoof_down_ >= cfg_.clear_epochs) {
      m_.spoofing = false;
      spoof_up_ = 0;
      push_event(EventKind::SpoofingEnd, "info", spoof_episode_reasons_);
    }
  }

  // ------------------------------------------------------------------ slow adaptation when clean
  const bool clean = !m_.jamming && !m_.spoofing && jam == 0 && spoof == 0 && fix;
  if (clean) {
    const double a = cfg_.adapt_alpha;
    cno_top_.adapt(m_.cno_top, a);
    strong_.adapt(m_.strong, a);
    if (m_.agc >= 0) agc_.adapt(m_.agc, a);
    if (m_.noise >= 0) noise_.adapt(m_.noise, a);
    if (m_.jamInd >= 0) jamind_.adapt(m_.jamInd, a);
    if (epoch_struct_valid_) {
      cno_std_.adapt(epoch_std_, a);
      corr_.adapt(epoch_corr_, a);
    }
    if (have_time) offset_.adapt(static_cast<double>(offset), a);  // tracks crystal drift of the local clock
    // The static reference position is deliberately NOT adapted: a slow "drag-off" attack must not move it.
  }
  if (fix) {
    have_prev_fix_ = true;
    prev_lat_ = m_.lat;
    prev_lon_ = m_.lon;
    prev_h_ = m_.height;
    prev_utc_ = m_.utc_ms;
    prev_local_ = local_ms;
  }
  if (have_time) {
    prev_offset_ = offset;
    have_prev_offset_ = true;
  }
  ready_flag_ = true;
}

// ------------------------------------------------------------------------------------------- JSON
std::string Detector::status_json() const {
  const char* state = m_.learning ? "learning"
                      : (m_.jamming && m_.spoofing) ? "jamming+spoofing"
                      : m_.jamming ? "jamming"
                      : m_.spoofing ? "spoofing"
                                    : "ok";
  char buf[640];
  std::snprintf(buf, sizeof(buf),
                "{\"type\":\"status\",\"epoch\":%u,\"utc\":%s,\"state\":\"%s\",\"fix\":%u,\"numSV\":%u,"
                "\"tracked\":%u,\"strong\":%u,\"lat\":%.7f,\"lon\":%.7f,\"h\":%.1f,\"hAcc\":%.1f,"
                "\"cno_top\":%.1f,\"cno_std\":%.2f,\"elev_corr\":%.2f,\"agc\":%d,\"noise\":%d,\"jamInd\":%d,"
                "\"ubx_jam\":%d,\"ubx_spoof\":%d,\"disp_m\":%.1f,\"speed\":%.1f,\"time_jump_ms\":%lld,"
                "\"jam_score\":%.1f,\"spoof_score\":%.1f,",
                m_.epoch, iso_utc(m_.utc_ms).c_str(), state, m_.fixType, m_.numSV, m_.tracked, m_.strong, m_.lat,
                m_.lon, m_.height, m_.hAcc, m_.cno_top, m_.cno_std, m_.elev_corr, m_.agc, m_.noise, m_.jamInd,
                m_.ubx_jam, m_.ubx_spoof, m_.displacement_m, m_.implied_speed,
                static_cast<long long>(m_.time_jump_ms), m_.jam_score, m_.spoof_score);
  std::string s(buf);
  s += "\"jam_reasons\":" + reasons_json(m_.jam_reasons) + ",\"spoof_reasons\":" + reasons_json(m_.spoof_reasons) + "}";
  return s;
}

std::string Detector::event_json(const Event& e) {
  char buf[400];
  std::snprintf(buf, sizeof(buf),
                "{\"type\":\"event\",\"event\":\"%s\",\"severity\":\"%s\",\"epoch\":%u,\"utc\":%s,\"lat\":%.7f,"
                "\"lon\":%.7f,\"cno_top\":%.1f,\"agc\":%d,\"jamInd\":%d,\"disp_m\":%.1f,\"jam_score\":%.1f,"
                "\"spoof_score\":%.1f,",
                event_name(e.kind), e.severity, e.m.epoch, iso_utc(e.m.utc_ms).c_str(), e.m.lat, e.m.lon,
                e.m.cno_top, e.m.agc, e.m.jamInd, e.m.displacement_m, e.m.jam_score, e.m.spoof_score);
  return std::string(buf) + "\"reasons\":" + reasons_json(e.reasons) + "}";
}

}  // namespace jw
