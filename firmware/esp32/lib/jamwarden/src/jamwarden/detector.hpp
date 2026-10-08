// JamWarden - GNSS jamming and spoofing detector.
//
// Learns a baseline of the RF front end (AGC, noise, CW jamming indicator) and of the signal structure
// (C/N0 level, spread and elevation dependence), then raises alarms with hysteresis when several independent
// indicators agree:
//   jamming  = RF front-end anomaly  AND  loss of signal quality (C/N0 drop, satellites or fix lost)
//   spoofing = position/time inconsistency OR receiver spoof flag OR (uniform, elevation-independent and
//              unusually strong signals)
#pragma once

#include <cstdint>
#include <string>

#include "jamwarden/messages.hpp"

namespace jw {

enum Reason : uint32_t {
  R_CNO_DROP = 1u << 0,
  R_AGC_ANOMALY = 1u << 1,
  R_NOISE_RISE = 1u << 2,
  R_JAMIND_HIGH = 1u << 3,
  R_UBX_JAM_WARN = 1u << 4,
  R_UBX_JAM_CRIT = 1u << 5,
  R_FIX_LOST = 1u << 6,
  R_SATS_LOST = 1u << 7,
  R_UBX_SPOOF = 1u << 8,
  R_POS_JUMP = 1u << 9,
  R_STATIC_DISPLACED = 1u << 10,
  R_SPEED_MISMATCH = 1u << 11,
  R_CNO_UNIFORM = 1u << 12,
  R_ELEV_CORR_LOW = 1u << 13,
  R_POWER_RISE = 1u << 14,
  R_TIME_JUMP = 1u << 15,
  R_ALT_JUMP = 1u << 16,
};
const char* reason_name(uint32_t bit);
std::string reasons_json(uint32_t mask);

struct DetectorConfig {
  bool static_mode = true;          // fixed installation (mast, roof, port crane) vs vehicle/vessel
  double static_radius_m = 50.0;    // allowed distance from the learned reference position
  double max_speed_mps = 70.0;      // mobile mode: fastest plausible platform speed
  double max_alt_jump_m = 100.0;
  int baseline_epochs = 60;         // epochs (s) used to learn the clean baseline
  double cno_drop_db = 6.0;
  double agc_sigma = 4.0;
  double agc_min_rel = 0.10;
  double noise_sigma = 4.0;
  double noise_min_rel = 0.20;
  int jamind_margin = 20;
  double uniform_std_db = 2.0;
  double elev_corr_min = 0.15;
  double baseline_corr_min = 0.30;
  double power_rise_db = 5.0;
  int64_t time_jump_ms = 1500;
  int raise_epochs = 3;             // consecutive suspicious epochs before an alarm
  int clear_epochs = 5;             // consecutive clean epochs before the alarm ends
  double adapt_alpha = 0.01;        // slow baseline adaptation while everything is clean
  int window = 10;                  // epochs for C/N0 structure features
  double jam_threshold = 3.5;
  double spoof_threshold = 3.0;
  int latch_epochs = 5;             // a one-off position/altitude jump stays an indicator this long
};

struct EpochMetrics {
  uint32_t epoch = 0;
  int64_t utc_ms = -1;
  uint32_t iTOW = 0;
  uint32_t local_ms = 0;
  uint8_t fixType = 0, numSV = 0, tracked = 0, strong = 0;
  double lat = 0, lon = 0, height = 0, hAcc = 0, gSpeed = 0;
  double cno_top = 0;     // mean C/N0 of the 6 strongest satellites
  double cno_std = 0;     // window mean of the C/N0 spread (satellites above 15 deg)
  double elev_corr = 0;   // window median of corr(elevation, C/N0)
  int agc = -1, noise = -1, jamInd = -1, ubx_jam = 0, ubx_spoof = 0;
  double displacement_m = 0, implied_speed = 0;
  int64_t time_jump_ms = 0;
  double jam_score = 0, spoof_score = 0;
  uint32_t jam_reasons = 0, spoof_reasons = 0;
  bool learning = true, jamming = false, spoofing = false;
};

enum class EventKind : uint8_t { BaselineReady, JammingStart, JammingEnd, SpoofingStart, SpoofingEnd };
const char* event_name(EventKind k);

struct Event {
  EventKind kind = EventKind::BaselineReady;
  const char* severity = "info";
  uint32_t reasons = 0;
  EpochMetrics m;
};

class Detector {
 public:
  explicit Detector(const DetectorConfig& cfg = DetectorConfig());

  // Feed every decoded UBX frame together with a local monotonic clock (millis()).
  void on_frame(const UbxFrame& f, uint32_t local_ms);
  // Events are queued; call until it returns false.
  bool poll_event(Event& out);
  // True exactly once after each processed navigation epoch.
  bool epoch_ready();
  const EpochMetrics& last() const { return m_; }
  const DetectorConfig& config() const { return cfg_; }
  // Optional: fix the static reference to a surveyed position instead of learning it.
  void set_reference(double lat, double lon, double height);

  std::string status_json() const;
  static std::string event_json(const Event& e);

 private:
  struct Stat {
    double mean = 0, var = 0;
    int n = 0;
    double m2 = 0;
    void learn(double x);
    void finish();
    void adapt(double x, double a);
    double sd() const;
  };
  void process_epoch(uint32_t local_ms);
  void push_event(EventKind k, const char* sev, uint32_t reasons);
  void compute_signal_features();

  DetectorConfig cfg_;
  NavPvt pvt_;
  NavSat sat_;
  NavStatus status_;
  RfInfo rf_;
  bool have_pvt_ = false, have_sat_ = false, have_status_ = false, have_rf_ = false, seen_eoe_ = false;
  uint32_t rf_local_ms_ = 0, last_processed_itow_ = 0xFFFFFFFF;
  bool processed_any_ = false;

  EpochMetrics m_;
  bool ready_flag_ = false;
  int learned_ = 0;
  Stat cno_top_, strong_, agc_, noise_, jamind_, cno_std_, corr_, offset_;
  double ref_lat_ = 0, ref_lon_ = 0, ref_h_ = 0;
  int ref_n_ = 0;
  bool ref_fixed_ = false;
  bool have_prev_fix_ = false;
  double prev_lat_ = 0, prev_lon_ = 0, prev_h_ = 0;
  int64_t prev_utc_ = -1;
  uint32_t prev_local_ = 0;
  bool have_prev_offset_ = false;
  int64_t prev_offset_ = 0;

  static constexpr int kWin = 32;
  double corr_win_[kWin] = {};
  double std_win_[kWin] = {};
  int win_n_ = 0, win_i_ = 0;
  double epoch_corr_ = 0, epoch_std_ = 0;
  bool epoch_struct_valid_ = false;

  int jam_up_ = 0, jam_down_ = 0, spoof_up_ = 0, spoof_down_ = 0;
  uint32_t latched_ = 0;
  int latch_left_ = 0;
  uint32_t jam_episode_reasons_ = 0, spoof_episode_reasons_ = 0;

  static constexpr int kQ = 16;
  Event q_[kQ];
  int q_head_ = 0, q_len_ = 0;
};

// Great-circle distance in metres.
double haversine_m(double lat1, double lon1, double lat2, double lon2);
// "2026-01-12T08:00:00.000Z" (or "null" if ms < 0)
std::string iso_utc(int64_t ms);

}  // namespace jw
