// jamwarden_replay - run the detector on a recorded/simulated UBX stream on a PC.
//
//   jamwarden_replay scenario.ubx                 # JSON lines (status every epoch + events)
//   jamwarden_replay --events-only scenario.ubx
//   cat /dev/ttyACM0 | jamwarden_replay -          # live from a receiver connected to the PC
//
// The local clock advances 1000 ms per navigation epoch (1 Hz) unless --rate-ms is given.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "jamwarden/detector.hpp"
#include "jamwarden/ubx.hpp"

namespace {
struct Ctx {
  jw::Detector* det;
  uint32_t local_ms = 0;
  uint32_t rate_ms = 1000;
  bool events_only = false;
  int status_every = 1;
  unsigned long epochs = 0;
};

void on_frame(const jw::UbxFrame& f, void* p) {
  Ctx* c = static_cast<Ctx*>(p);
  if (f.cls == jw::cls::NAV && f.id == jw::id::NAV_PVT) c->local_ms += c->rate_ms;
  c->det->on_frame(f, c->local_ms);
  jw::Event e;
  while (c->det->poll_event(e)) std::printf("%s\n", jw::Detector::event_json(e).c_str());
  if (c->det->epoch_ready()) {
    ++c->epochs;
    if (!c->events_only && (c->epochs % static_cast<unsigned long>(c->status_every) == 0))
      std::printf("%s\n", c->det->status_json().c_str());
  }
}

void usage() {
  std::fprintf(stderr,
               "usage: jamwarden_replay [--events-only] [--status-every N] [--mobile] [--baseline N]\n"
               "                        [--rate-ms MS] [--ref LAT,LON,H] FILE|-\n");
}
}  // namespace

int main(int argc, char** argv) {
  jw::DetectorConfig cfg;
  Ctx ctx;
  const char* path = nullptr;
  double ref[3] = {0, 0, 0};
  bool have_ref = false;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--events-only") ctx.events_only = true;
    else if (a == "--mobile") cfg.static_mode = false;
    else if (a == "--status-every" && i + 1 < argc) ctx.status_every = std::max(1, std::atoi(argv[++i]));
    else if (a == "--baseline" && i + 1 < argc) cfg.baseline_epochs = std::atoi(argv[++i]);
    else if (a == "--rate-ms" && i + 1 < argc) ctx.rate_ms = static_cast<uint32_t>(std::atoi(argv[++i]));
    else if (a == "--ref" && i + 1 < argc) {
      have_ref = std::sscanf(argv[++i], "%lf,%lf,%lf", &ref[0], &ref[1], &ref[2]) == 3;
    } else if (a == "-h" || a == "--help") {
      usage();
      return 0;
    } else path = argv[i];
  }
  if (!path) {
    usage();
    return 2;
  }
  FILE* in = (std::strcmp(path, "-") == 0) ? stdin : std::fopen(path, "rb");
  if (!in) {
    std::perror(path);
    return 1;
  }
  jw::Detector det(cfg);
  if (have_ref) det.set_reference(ref[0], ref[1], ref[2]);
  ctx.det = &det;
  jw::UbxParser parser;
  parser.set_callback(on_frame, &ctx);
  unsigned char buf[4096];
  size_t n;
  while ((n = std::fread(buf, 1, sizeof(buf), in)) > 0) parser.feed(buf, n);
  if (in != stdin) std::fclose(in);
  std::printf("{\"type\":\"summary\",\"frames\":%u,\"checksum_errors\":%u,\"epochs\":%lu}\n", parser.frames(),
              parser.checksum_errors(), ctx.epochs);
  return 0;
}
