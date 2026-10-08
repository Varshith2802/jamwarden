// Self-contained unit tests (no framework needed): build with CMake/Make and run ./jamwarden_tests
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "jamwarden/detector.hpp"
#include "jamwarden/messages.hpp"
#include "jamwarden/ubx.hpp"

static int g_fail = 0, g_pass = 0;
#define CHECK(cond)                                                                \
  do {                                                                             \
    if (cond) {                                                                    \
      ++g_pass;                                                                    \
    } else {                                                                       \
      ++g_fail;                                                                    \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                \
    }                                                                              \
  } while (0)

using Bytes = std::vector<uint8_t>;

static void put16(Bytes& b, size_t o, uint16_t v) { b[o] = v & 0xFF; b[o + 1] = v >> 8; }
static void put32(Bytes& b, size_t o, uint32_t v) {
  for (int i = 0; i < 4; ++i) b[o + i] = (v >> (8 * i)) & 0xFF;
}
static Bytes frame(uint8_t c, uint8_t i, const Bytes& p) {
  Bytes out(p.size() + 8);
  out.resize(jw::ubx_encode(c, i, p.data(), static_cast<uint16_t>(p.size()), out.data(), out.size()));
  return out;
}

// ------------------------------------------------------------------ tiny receiver simulator
struct EpochSpec {
  double lat = 56.1612, lon = 15.5869, h = 30.0;
  double cno_offset = 0;   // dB added to every satellite
  bool uniform = false;    // spoofer: same power for all satellites
  int lose_below = 0;      // satellites weaker than this are lost
  uint8_t fix = 3;
  uint16_t agc = 4500, noise = 90;
  uint8_t jamInd = 5, jamState = 1, spoofState = 1;
  int64_t time_offset_ms = 0;
};

struct Sim {
  int k = 0;
  uint32_t seed = 1;
  double rnd() {  // deterministic LCG in [-1, 1]
    seed = seed * 1664525u + 1013904223u;
    return ((seed >> 8) & 0xFFFF) / 32767.5 - 1.0;
  }
  Bytes epoch(const EpochSpec& s, bool with_eoe = false, bool pvt_first = false) {
    const int64_t base = 1768204800000LL;  // 2026-01-12T08:00:00Z
    const int64_t t = base + 1000LL * k + s.time_offset_ms;
    const uint32_t itow = static_cast<uint32_t>((t / 1000 % 604800) * 1000);
    // NAV-SAT: 12 satellites, elevations 10..80 deg, C/N0 rising with elevation
    const int n = 12;
    Bytes sat(8 + 12 * n, 0);
    put32(sat, 0, itow);
    sat[4] = 1;
    sat[5] = n;
    int used = 0;
    for (int i = 0; i < n; ++i) {
      const int elev = 10 + i * 6;
      double cno = s.uniform ? 46.0 + 0.4 * rnd() : 26.0 + 0.27 * elev + 1.5 * rnd();
      cno += s.cno_offset;
      if (cno < s.lose_below || cno < 12) cno = 0;
      const size_t o = 8 + 12 * i;
      sat[o] = i < 8 ? 0 : 2;
      sat[o + 1] = static_cast<uint8_t>(i + 1);
      sat[o + 2] = static_cast<uint8_t>(cno);
      sat[o + 3] = static_cast<uint8_t>(static_cast<int8_t>(elev));
      put16(sat, o + 4, static_cast<uint16_t>(i * 30));
      put32(sat, o + 8, cno > 0 ? 0x0F : 0x00);
      if (cno > 0) ++used;
    }
    Bytes st(16, 0);
    put32(st, 0, itow);
    st[4] = s.fix;
    st[5] = s.fix >= 3 ? 0x01 : 0;
    st[7] = static_cast<uint8_t>((s.spoofState & 3) << 3);
    Bytes rf(4 + 24, 0);
    rf[1] = 1;
    rf[4 + 1] = s.jamState & 3;
    put16(rf, 4 + 12, s.noise);
    put16(rf, 4 + 14, s.agc);
    rf[4 + 16] = s.jamInd;
    Bytes pvt(92, 0);
    put32(pvt, 0, itow);
    const int64_t secs = t / 1000;
    const int sod = static_cast<int>(secs % 86400);
    put16(pvt, 4, 2026);
    pvt[6] = 1;
    pvt[7] = static_cast<uint8_t>(12 + (secs - base / 1000 + 8 * 3600) / 86400);
    pvt[8] = static_cast<uint8_t>(sod / 3600);
    pvt[9] = static_cast<uint8_t>((sod % 3600) / 60);
    pvt[10] = static_cast<uint8_t>(sod % 60);
    pvt[11] = 0x07;
    pvt[20] = s.fix;
    pvt[21] = s.fix >= 3 ? 0x01 : 0x00;
    pvt[23] = static_cast<uint8_t>(used);
    put32(pvt, 24, static_cast<uint32_t>(static_cast<int32_t>(std::lround((s.lon + 0.00001 * rnd()) * 1e7))));
    put32(pvt, 28, static_cast<uint32_t>(static_cast<int32_t>(std::lround((s.lat + 0.00001 * rnd()) * 1e7))));
    put32(pvt, 32, static_cast<uint32_t>(static_cast<int32_t>(s.h * 1000)));
    put32(pvt, 40, 2000);
    put32(pvt, 44, 3000);
    Bytes out;
    auto app = [&](const Bytes& f) { out.insert(out.end(), f.begin(), f.end()); };
    if (pvt_first) app(frame(jw::cls::NAV, jw::id::NAV_PVT, pvt));
    app(frame(jw::cls::NAV, jw::id::NAV_SAT, sat));
    app(frame(jw::cls::NAV, jw::id::NAV_STATUS, st));
    app(frame(jw::cls::MON, jw::id::MON_RF, rf));
    if (!pvt_first) app(frame(jw::cls::NAV, jw::id::NAV_PVT, pvt));
    if (with_eoe) {
      Bytes eoe(4, 0);
      put32(eoe, 0, itow);
      app(frame(jw::cls::NAV, jw::id::NAV_EOE, eoe));
    }
    ++k;
    return out;
  }
};

struct Harness {
  jw::Detector det;
  jw::UbxParser parser;
  uint32_t local = 0;
  std::vector<std::pair<std::string, uint32_t>> events;  // name, epoch
  std::vector<uint32_t> reasons;
  explicit Harness(const jw::DetectorConfig& c = jw::DetectorConfig()) : det(c) {
    parser.set_callback(
        [](const jw::UbxFrame& f, void* p) {
          Harness* h = static_cast<Harness*>(p);
          if (f.cls == jw::cls::NAV && f.id == jw::id::NAV_PVT) h->local += 1000;
          h->det.on_frame(f, h->local);
          jw::Event e;
          while (h->det.poll_event(e)) {
            h->events.emplace_back(jw::event_name(e.kind), e.m.epoch);
            h->reasons.push_back(e.reasons);
          }
        },
        this);
  }
  void feed(const Bytes& b) { parser.feed(b.data(), b.size()); }
  int count(const std::string& name) const {
    int n = 0;
    for (auto& e : events) n += e.first == name;
    return n;
  }
  uint32_t first(const std::string& name) const {
    for (auto& e : events)
      if (e.first == name) return e.second;
    return 0;
  }
};

// ------------------------------------------------------------------ tests
static void test_parser() {
  Bytes p = {1, 2, 3, 4};
  Bytes f = frame(0x01, 0x07, p);
  CHECK(f.size() == 12);
  struct Got { int n = 0; uint8_t cls = 0, id = 0; uint16_t len = 0; } got;
  jw::UbxParser parser;
  parser.set_callback([](const jw::UbxFrame& fr, void* c) {
    Got* g = static_cast<Got*>(c);
    ++g->n; g->cls = fr.cls; g->id = fr.id; g->len = fr.len;
  }, &got);
  Bytes noisy = {'$', 'G', 'P', 0xB5, 0x00, 0xB5};  // NMEA junk and false syncs before the frame
  parser.feed(noisy.data(), noisy.size());
  parser.feed(f.data(), f.size());
  CHECK(got.n == 1 && got.cls == 0x01 && got.id == 0x07 && got.len == 4);
  Bytes bad = f;
  bad[7] ^= 0xFF;  // corrupt payload
  parser.feed(bad.data(), bad.size());
  CHECK(parser.checksum_errors() == 1 && got.n == 1);
  parser.feed(f.data(), f.size());
  CHECK(got.n == 2);
}

static void test_decoders() {
  Bytes pvt(92, 0);
  put32(pvt, 0, 123000);
  put16(pvt, 4, 2026);
  pvt[6] = 1; pvt[7] = 12; pvt[8] = 8; pvt[9] = 0; pvt[10] = 0; pvt[11] = 0x07;
  pvt[20] = 3; pvt[21] = 1; pvt[23] = 17;
  put32(pvt, 24, static_cast<uint32_t>(155869000));
  put32(pvt, 28, static_cast<uint32_t>(561612000));
  put32(pvt, 32, 30500);
  put32(pvt, 40, 1500);
  put32(pvt, 60, static_cast<uint32_t>(-2500));
  Bytes f = frame(jw::cls::NAV, jw::id::NAV_PVT, pvt);
  jw::UbxFrame fr; fr.cls = f[2]; fr.id = f[3]; fr.len = 92; fr.payload = f.data() + 6;
  jw::NavPvt o;
  CHECK(jw::decode_nav_pvt(fr, o));
  CHECK(o.iTOW == 123000 && o.numSV == 17 && o.fixType == 3 && o.gnss_fix_ok());
  CHECK(std::fabs(o.lat_deg - 56.1612) < 1e-9 && std::fabs(o.lon_deg - 15.5869) < 1e-9);
  CHECK(std::fabs(o.height_m - 30.5) < 1e-9 && std::fabs(o.hAcc_m - 1.5) < 1e-9 && std::fabs(o.gSpeed + 2.5) < 1e-9);
  CHECK(jw::pvt_unix_ms(o) == 1768204800000LL);

  Bytes st(16, 0);
  st[7] = 2 << 3;
  jw::UbxFrame sf; sf.cls = jw::cls::NAV; sf.id = jw::id::NAV_STATUS; sf.len = 16; sf.payload = st.data();
  jw::NavStatus ns;
  CHECK(jw::decode_nav_status(sf, ns) && ns.spoofDetState == 2);

  Bytes hw(60, 0);
  put16(hw, 16, 77); put16(hw, 18, 3100); hw[22] = 3 << 2; hw[45] = 42;
  jw::UbxFrame hf; hf.cls = jw::cls::MON; hf.id = jw::id::MON_HW; hf.len = 60; hf.payload = hw.data();
  jw::RfInfo rf;
  CHECK(jw::decode_mon_hw(hf, rf) && rf.agcCnt == 3100 && rf.noisePerMS == 77 && rf.jammingState == 3 && rf.jamInd == 42);
  CHECK(std::fabs(jw::haversine_m(56.0, 15.0, 57.0, 15.0) - 111195.0) < 50.0);
  CHECK(jw::iso_utc(1768204800123LL) == "\"2026-01-12T08:00:00.123Z\"");
}

static void run_scenario(Harness& h, Sim& sim, int n, const EpochSpec& s) {
  for (int i = 0; i < n; ++i) h.feed(sim.epoch(s));
}

static void test_clean_has_no_alarms() {
  Harness h; Sim sim;
  run_scenario(h, sim, 400, EpochSpec());
  CHECK(h.count("baseline_ready") == 1);
  CHECK(h.events.size() == 1);
  CHECK(!h.det.last().learning && h.det.last().elev_corr > 0.5);
}

static void test_jamming_detected_and_cleared() {
  Harness h; Sim sim;
  run_scenario(h, sim, 100, EpochSpec());
  EpochSpec jam;
  jam.cno_offset = -14; jam.agc = 1500; jam.noise = 210; jam.jamInd = 60; jam.jamState = 2;
  run_scenario(h, sim, 60, jam);
  run_scenario(h, sim, 30, EpochSpec());
  CHECK(h.count("jamming_start") == 1 && h.count("jamming_end") == 1);
  CHECK(h.first("jamming_start") >= 101 && h.first("jamming_start") <= 104);
  CHECK(h.first("jamming_end") >= 161 && h.first("jamming_end") <= 168);
  CHECK(h.count("spoofing_start") == 0);
}

static void test_obstruction_is_not_jamming() {
  Harness h; Sim sim;
  run_scenario(h, sim, 100, EpochSpec());
  EpochSpec tunnel;
  tunnel.cno_offset = -18; tunnel.lose_below = 20;  // sky blocked: weak signals, but RF front end normal
  run_scenario(h, sim, 60, tunnel);
  CHECK(h.count("jamming_start") == 0);
}

static void test_spoofing_signal_structure_and_drag() {
  Harness h; Sim sim;
  run_scenario(h, sim, 100, EpochSpec());
  EpochSpec sp;
  sp.uniform = true;
  for (int i = 0; i < 60; ++i) {
    sp.lon = 15.5869 + i * 0.00004;  // ~2.5 m/s eastward drag-off
    h.feed(sim.epoch(sp));
  }
  CHECK(h.count("spoofing_start") == 1);
  CHECK(h.first("spoofing_start") <= 110);
  CHECK(h.count("jamming_start") == 0);
  const jw::EpochMetrics& m = h.det.last();
  CHECK(m.spoof_reasons & jw::R_STATIC_DISPLACED);
  CHECK(m.spoof_reasons & jw::R_ELEV_CORR_LOW);
}

static void test_time_jump() {
  Harness h; Sim sim;
  run_scenario(h, sim, 100, EpochSpec());
  EpochSpec t;
  t.time_offset_ms = 3000;
  run_scenario(h, sim, 20, t);
  CHECK(h.count("spoofing_start") == 1);
  CHECK(!h.reasons.empty() && (h.reasons.back() & jw::R_TIME_JUMP));
}

static void test_receiver_spoof_flag() {
  Harness h; Sim sim;
  run_scenario(h, sim, 100, EpochSpec());
  EpochSpec f;
  f.spoofState = 3;
  run_scenario(h, sim, 10, f);
  CHECK(h.count("spoofing_start") == 1);
}

static void test_eoe_ordering() {
  // PVT arrives before NAV-SAT; with NAV-EOE the epoch is processed at the end, using fresh NAV-SAT.
  Harness h; Sim sim;
  for (int i = 0; i < 80; ++i) h.feed(sim.epoch(EpochSpec(), true, true));
  CHECK(!h.det.last().learning);
  CHECK(h.det.last().tracked == 12);
  CHECK(h.det.last().epoch == 80);
}

int main() {
  test_parser();
  test_decoders();
  test_clean_has_no_alarms();
  test_jamming_detected_and_cleared();
  test_obstruction_is_not_jamming();
  test_spoofing_signal_structure_and_drag();
  test_time_jump();
  test_receiver_spoof_flag();
  test_eoe_ordering();
  std::printf("%d passed, %d failed\n", g_pass, g_fail);
  return g_fail ? 1 : 0;
}
