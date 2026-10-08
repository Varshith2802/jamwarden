#!/usr/bin/env python3
"""Plot a replayed session: signal metrics, RF front end, scores and alarms vs ground truth.

    python tools/plot_session.py data/scenarios/jam_then_spoof.ubx --out docs/session.png
    python tools/plot_session.py --jsonl capture.jsonl --out session.png      # output of the device / replay
"""
from __future__ import annotations

import argparse
import json
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
REPLAY = ROOT / "core" / "build" / "jamwarden_replay"


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("ubx", nargs="?", type=Path)
    ap.add_argument("--jsonl", type=Path, help="use existing JSON lines instead of running the replay tool")
    ap.add_argument("--mobile", action="store_true")
    ap.add_argument("--out", type=Path, default=Path("session.png"))
    a = ap.parse_args()
    if a.jsonl:
        lines = [json.loads(l) for l in a.jsonl.read_text().splitlines() if l.strip()]
        truth = []
    else:
        cmd = [str(REPLAY)] + (["--mobile"] if a.mobile else []) + [str(a.ubx)]
        lines = [json.loads(l) for l in subprocess.run(cmd, capture_output=True, text=True, check=True).stdout.splitlines()]
        tf = a.ubx.with_suffix(".truth.json")
        truth = json.loads(tf.read_text())["truth"] if tf.exists() else []
    st = [l for l in lines if l["type"] == "status"]
    ev = [l for l in lines if l["type"] == "event" and l["event"] != "baseline_ready"]

    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    t = [s["epoch"] for s in st]
    fig, ax = plt.subplots(5, 1, figsize=(11, 10), sharex=True)
    ax[0].plot(t, [s["cno_top"] for s in st], label="mean C/N0, 6 strongest (dB-Hz)")
    ax[0].plot(t, [s["tracked"] for s in st], label="satellites tracked", alpha=0.7)
    ax[0].legend(fontsize=8, loc="lower left")
    ax[1].plot(t, [s["cno_std"] for s in st], label="C/N0 spread (dB)")
    ax[1].plot(t, [s["elev_corr"] for s in st], label="corr(elevation, C/N0)")
    ax[1].legend(fontsize=8, loc="lower left")
    ax[2].plot(t, [s["agc"] for s in st], label="AGC count")
    ax2 = ax[2].twinx()
    ax2.plot(t, [s["noise"] for s in st], color="tab:orange", label="noise per ms")
    ax2.plot(t, [s["jamInd"] for s in st], color="tab:red", label="CW jamming indicator")
    ax[2].legend(fontsize=8, loc="lower left")
    ax2.legend(fontsize=8, loc="lower right")
    ax[3].plot(t, [max(s["disp_m"], 0.1) for s in st], label="distance from reference (m)")
    ax[3].set_yscale("log")
    ax[3].legend(fontsize=8, loc="upper left")
    ax[4].plot(t, [s["jam_score"] for s in st], label="jamming score")
    ax[4].plot(t, [s["spoof_score"] for s in st], label="spoofing score")
    ax[4].axhline(3.5, color="tab:blue", ls=":", lw=0.8)
    ax[4].axhline(3.0, color="tab:orange", ls=":", lw=0.8)
    ax[4].legend(fontsize=8, loc="upper left")
    ax[4].set_xlabel("time (s)")
    colors = {"jamming": "tab:red", "spoofing": "tab:purple", "negative": "tab:gray"}
    for tr in truth:
        for a_ in ax:
            a_.axvspan(tr["start"], tr["end"], color=colors[tr["type"]], alpha=0.08)
    for e in ev:
        for a_ in ax:
            a_.axvline(e["epoch"], color="k" if e["event"].endswith("start") else "0.6", lw=0.8,
                       ls="-" if e["event"].endswith("start") else "--")
        ax[0].annotate(e["event"], (e["epoch"], ax[0].get_ylim()[1]), fontsize=7, rotation=90, va="top", ha="right")
    title = a.ubx.name if a.ubx else a.jsonl.name
    fig.suptitle(f"JamWarden session: {title} (shaded = ground truth, lines = alarms)", fontsize=11)
    fig.tight_layout()
    fig.savefig(a.out, dpi=150)
    print(f"wrote {a.out}")


if __name__ == "__main__":
    main()
