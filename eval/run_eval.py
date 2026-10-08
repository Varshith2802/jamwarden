#!/usr/bin/env python3
"""Detection performance of JamWarden on simulated scenarios (many random seeds).

    python eval/run_eval.py --seeds 20            # needs core/build/jamwarden_replay (see README)

For every attack interval in the ground truth:
  * detected      - an alarm of the right type starts inside [start, end + 10 s]
  * latency       - alarm epoch - attack start (s)
  * flags_only    - the receiver's own jamming/spoofing flags alone (>= 3 consecutive epochs), as a baseline;
                    each run randomly simulates a receiver with or without these flags
False alarms: alarm starts outside every ground-truth interval of that type (10 s grace), per hour.
"""
from __future__ import annotations

import argparse
import json
import statistics
import subprocess
import sys
import tempfile
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from scenarios import MOBILE, SCENARIOS, generate  # noqa: E402

REPLAY = ROOT / "core" / "build" / "jamwarden_replay"
GRACE = 10


def run_replay(path: Path, mobile: bool = False) -> list[dict]:
    cmd = [str(REPLAY)] + (["--mobile"] if mobile else []) + [str(path)]
    out = subprocess.run(cmd, capture_output=True, text=True, check=True).stdout
    return [json.loads(l) for l in out.splitlines() if l.strip()]


def flags_only_detect(status: list[dict], kind: str, start: int, end: int) -> int | None:
    key = "ubx_jam" if kind == "jamming" else "ubx_spoof"
    run = 0
    for s in status:
        if start <= s["epoch"] <= end + GRACE:
            run = run + 1 if s[key] >= 2 else 0
            if run >= 3:
                return s["epoch"]
    return None


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--seeds", type=int, default=20)
    ap.add_argument("--duration", type=int, default=900)
    ap.add_argument("--out", type=Path, default=ROOT / "eval" / "results")
    a = ap.parse_args()
    if not REPLAY.exists():
        sys.exit(f"build the core first: cmake -S core -B core/build && cmake --build core/build  ({REPLAY} missing)")

    per_scn: dict[str, dict] = defaultdict(lambda: {"attacks": 0, "detected": 0, "flags_detected": 0,
                                                    "latencies": [], "false_alarms": 0, "runs": 0})
    total_hours = 0.0
    with tempfile.TemporaryDirectory() as tmp:
        for name in SCENARIOS:
            for seed in range(1, a.seeds + 1):
                data, meta = generate(name, seed, a.duration)
                f = Path(tmp) / f"{name}_{seed}.ubx"
                f.write_bytes(data)
                lines = run_replay(f, mobile=name in MOBILE)
                status = [l for l in lines if l["type"] == "status"]
                starts = [l for l in lines if l["type"] == "event" and l["event"].endswith("_start")]
                s = per_scn[name]
                s["runs"] += 1
                total_hours += a.duration / 3600
                truth = [t for t in meta["truth"] if t["type"] in ("jamming", "spoofing")]
                for t in truth:
                    s["attacks"] += 1
                    hits = [e for e in starts if e["event"].startswith(t["type"][:4])
                            and t["start"] <= e["epoch"] <= t["end"] + GRACE]
                    if hits:
                        s["detected"] += 1
                        s["latencies"].append(hits[0]["epoch"] - t["start"])
                    if flags_only_detect(status, t["type"], t["start"], t["end"]) is not None:
                        s["flags_detected"] += 1
                for e in starts:
                    kind = "jamming" if e["event"].startswith("jam") else "spoofing"
                    ok = any(t["type"] == kind and t["start"] - GRACE <= e["epoch"] <= t["end"] + GRACE for t in truth)
                    if not ok:
                        s["false_alarms"] += 1
            print(f"  {name:16s} done", flush=True)

    rows = []
    for name, s in per_scn.items():
        lat = s["latencies"]
        rows.append({
            "scenario": name, "runs": s["runs"], "attacks": s["attacks"],
            "detection_rate": round(s["detected"] / s["attacks"], 3) if s["attacks"] else None,
            "flags_only_rate": round(s["flags_detected"] / s["attacks"], 3) if s["attacks"] else None,
            "latency_median_s": statistics.median(lat) if lat else None,
            "latency_p90_s": sorted(lat)[int(0.9 * (len(lat) - 1))] if lat else None,
            "false_alarms": s["false_alarms"],
        })
    fa_total = sum(r["false_alarms"] for r in rows)
    summary = {"seeds": a.seeds, "duration_s": a.duration, "simulated_hours": round(total_hours, 2),
               "false_alarms_total": fa_total, "false_alarms_per_hour": round(fa_total / total_hours, 3),
               "scenarios": rows}
    a.out.mkdir(parents=True, exist_ok=True)
    (a.out / "summary.json").write_text(json.dumps(summary, indent=2))
    md = ["| scenario | runs | attacks | JamWarden detection | receiver flags only | median latency (s) | p90 latency (s) | false alarms |",
          "|---|---|---|---|---|---|---|---|"]
    for r in rows:
        pct = lambda v: "-" if v is None else f"{100 * v:.0f} %"
        md.append(f"| {r['scenario']} | {r['runs']} | {r['attacks']} | {pct(r['detection_rate'])} | "
                  f"{pct(r['flags_only_rate'])} | {r['latency_median_s'] if r['latency_median_s'] is not None else '-'} | "
                  f"{r['latency_p90_s'] if r['latency_p90_s'] is not None else '-'} | {r['false_alarms']} |")
    md.append("")
    md.append(f"Simulated time: {summary['simulated_hours']} h - false alarms: {fa_total} "
              f"({summary['false_alarms_per_hour']} per hour)")
    (a.out / "summary.md").write_text("\n".join(md) + "\n")
    print("\n".join(md))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
