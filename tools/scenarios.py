#!/usr/bin/env python3
"""Generate realistic UBX streams (NAV-SAT, NAV-STATUS, MON-RF, NAV-PVT at 1 Hz) for jamming/spoofing scenarios.

    python tools/scenarios.py list
    python tools/scenarios.py generate jam_broadband --seed 3 --out data/scenarios/jam_broadband_3.ubx
    python tools/scenarios.py generate-all --out data/scenarios

Each .ubx file gets a <name>.truth.json with the ground-truth attack intervals (epoch numbers, 1-based).

Signal model (per satellite): C/N0 = 28 + 0.22*elevation + multipath noise (stronger at low elevation),
satellites rise and set, AGC/noise/CW-indicator wander slowly. Attacks change the physics the way the
literature describes them: jammers raise the noise floor (AGC drops, noise rises, C/N0 falls, weak satellites
are lost); spoofers transmit all satellites from one antenna (uniform, elevation-independent and often
stronger C/N0) and then move the solution (drag-off or jump) or shift time.
"""
from __future__ import annotations

import argparse
import json
import math
import random
import struct
from dataclasses import dataclass, field
from pathlib import Path

BASE_UTC = 1768204800  # 2026-01-12T08:00:00Z
REF_LAT, REF_LON, REF_H = 56.1612, 15.5869, 32.0  # Karlskrona harbour


def ubx(cls: int, mid: int, payload: bytes) -> bytes:
    body = bytes([cls, mid]) + struct.pack("<H", len(payload)) + payload
    a = b = 0
    for x in body:
        a = (a + x) & 0xFF
        b = (b + a) & 0xFF
    return b"\xb5\x62" + body + bytes([a, b])


@dataclass
class Sat:
    gnss: int
    sv: int
    elev0: float
    rate: float     # deg/s (positive = rising)
    azim: float


@dataclass
class Attack:
    kind: str                    # jam_broadband | jam_cw | spoof_drag | spoof_jump | spoof_time | obstruction | multipath
    start: int
    end: int | None = None
    params: dict = field(default_factory=dict)


SCENARIOS = {
    "clean": "no interference (false-alarm measurement)",
    "jam_broadband": "broadband jammer ramping up to 12-25 dB",
    "jam_cw": "continuous-wave jammer (high CW indicator, moderate C/N0 loss)",
    "spoof_drag": "spoofer takes over and slowly drags the position away (accelerating to 5 m/s)",
    "spoof_jump": "spoofer moves the position 30-80 km away (Baltic 'airport' pattern)",
    "spoof_time": "spoofer shifts GNSS time by 2-10 s (attack on timing receivers)",
    "time_only": "time shift without any change in signal structure",
    "jam_then_spoof": "jamming forces re-acquisition, then a spoofer drags the position",
    "obstruction": "negative: half the sky blocked (bridge/crane), RF front end normal",
    "multipath": "negative: strong multipath, C/N0 variance x3",
    "weak_jam": "hard: low-power jammer (4-8 dB) - near the detection limit",
    "smart_spoof_slow": "hard: power-matched, elevation-aware spoofer dragging at 0.3 m/s",
    "ship_clean": "vessel moving at 6 m/s (mobile mode) - false-alarm measurement",
    "ship_spoof_jump": "vessel: position jumps 30-80 km (Baltic pattern), motion continues",
}

#: scenarios that must be replayed with --mobile
MOBILE = {"ship_clean", "ship_spoof_jump"}


def make_attacks(name: str, rng: random.Random, duration: int) -> list[Attack]:
    t1 = rng.randint(240, 360)
    if name == "clean":
        return []
    if name == "jam_broadband":
        return [Attack("jam_broadband", t1, t1 + rng.randint(90, 180), {"peak_db": rng.uniform(12, 25)})]
    if name == "jam_cw":
        return [Attack("jam_cw", t1, t1 + rng.randint(90, 180), {"peak_db": rng.uniform(8, 15),
                                                                   "jamind": rng.randint(120, 220)})]
    if name == "spoof_drag":
        return [Attack("spoof_drag", t1, None, {"accel": rng.uniform(0.03, 0.08), "vmax": 5.0,
                                                "heading": rng.uniform(0, 360), "power_db": rng.uniform(3, 8)})]
    if name == "spoof_jump":
        return [Attack("spoof_jump", t1, None, {"dist_km": rng.uniform(30, 80), "heading": rng.uniform(0, 360),
                                                "power_db": rng.uniform(3, 8)})]
    if name == "spoof_time":
        return [Attack("spoof_time", t1, None, {"shift_ms": rng.choice([1, -1]) * rng.randint(2000, 10000),
                                                "power_db": rng.uniform(3, 8)})]
    if name == "time_only":
        return [Attack("time_only", t1, None, {"shift_ms": rng.choice([1, -1]) * rng.randint(2000, 10000)})]
    if name == "jam_then_spoof":
        j_end = t1 + rng.randint(40, 70)
        return [Attack("jam_broadband", t1, j_end, {"peak_db": rng.uniform(15, 25)}),
                Attack("spoof_drag", j_end, None, {"accel": rng.uniform(0.03, 0.08), "vmax": 5.0,
                                                   "heading": rng.uniform(0, 360), "power_db": rng.uniform(3, 8)})]
    if name == "weak_jam":
        return [Attack("jam_broadband", t1, t1 + rng.randint(90, 180), {"peak_db": rng.uniform(4, 8)})]
    if name == "smart_spoof_slow":
        return [Attack("spoof_smart", t1, None, {"speed": 0.3, "heading": rng.uniform(0, 360),
                                                 "power_db": rng.uniform(1, 3)})]
    if name == "ship_clean":
        return []
    if name == "ship_spoof_jump":
        return [Attack("spoof_jump", t1, None, {"dist_km": rng.uniform(30, 80), "heading": rng.uniform(0, 360),
                                                "power_db": rng.uniform(3, 8)})]
    if name == "obstruction":
        return [Attack("obstruction", t1, t1 + rng.randint(60, 150), {"sector": rng.uniform(0, 360)})]
    if name == "multipath":
        return [Attack("multipath", t1, t1 + rng.randint(120, 240), {})]
    raise ValueError(name)


def generate(name: str, seed: int, duration: int = 900, ubx_flags: bool | None = None) -> tuple[bytes, dict]:
    rng = random.Random(f"{name}-{seed}")
    flags_on = rng.random() < 0.5 if ubx_flags is None else ubx_flags
    attacks = make_attacks(name, rng, duration)
    sats = []
    for g, count in ((0, 10), (2, 8), (6, 6), (3, 6)):  # GPS, Galileo, GLONASS, BeiDou
        for i in range(count):
            sats.append(Sat(g, i + 1, rng.uniform(-10, 85), rng.uniform(-0.004, 0.004), rng.uniform(0, 360)))
    mp = {(s.gnss, s.sv): 0.0 for s in sats}
    agc_base, noise_base, jam_base = rng.uniform(3500, 5500), rng.uniform(80, 110), rng.uniform(2, 8)
    agc_walk = 0.0
    lat, lon, h = REF_LAT, REF_LON, REF_H
    ship = name in MOBILE
    ship_heading = math.radians(rng.uniform(0, 360))
    ship_speed = 6.0
    spoof_v = 0.0
    spoof_off_n = spoof_off_e = 0.0
    out = bytearray()
    truth = []
    for a in attacks:
        kind = ("jamming" if a.kind.startswith("jam") else "spoofing" if a.kind.startswith(("spoof", "time"))
                else "negative")
        truth.append({"type": kind, "kind": a.kind, "start": a.start, "end": a.end or duration, **{
            k: (round(v, 3) if isinstance(v, float) else v) for k, v in a.params.items()}})

    for t in range(1, duration + 1):
        jam_db = 0.0
        jamind_extra = 0.0
        spoofing = None
        time_shift = 0
        blocked_sector = None
        mp_scale = 1.0
        for a in attacks:
            end = a.end or duration
            if not (a.start <= t <= end):
                continue
            ramp = min(1.0, (t - a.start) / 30.0, (end - t) / 20.0 + 0.05) if a.end else min(1.0, (t - a.start) / 5.0)
            if a.kind == "jam_broadband":
                jam_db = max(jam_db, a.params["peak_db"] * max(0.0, ramp))
                jamind_extra = max(jamind_extra, 8 * ramp)
            elif a.kind == "jam_cw":
                jam_db = max(jam_db, a.params["peak_db"] * max(0.0, ramp))
                jamind_extra = max(jamind_extra, a.params["jamind"] * ramp)
            elif a.kind in ("spoof_drag", "spoof_jump", "spoof_time", "spoof_smart"):
                spoofing = a
                if a.kind == "spoof_time":
                    time_shift = a.params["shift_ms"]
            elif a.kind == "time_only":
                time_shift = a.params["shift_ms"]
            elif a.kind == "obstruction":
                blocked_sector = a.params["sector"]
            elif a.kind == "multipath":
                mp_scale = 3.0

        # ---------------- RF front end
        agc_walk = max(-150.0, min(150.0, agc_walk + rng.gauss(0, 2)))
        agc = agc_base + agc_walk + rng.gauss(0, 25)
        noise = noise_base + rng.gauss(0, 2.5)
        jam_ind = max(0.0, jam_base + rng.gauss(0, 1.5))
        if jam_db > 0:
            agc *= 1 - 0.6 * min(jam_db, 25) / 25
            noise *= 1 + 1.6 * min(jam_db, 25) / 25
            jam_ind += jamind_extra
        if spoofing is not None:
            agc *= 0.95  # spoofer adds a little power
        jam_state = 1
        if flags_on and jam_db > 10:
            jam_state = 3 if jam_db > 18 else 2

        # ---------------- satellites
        itow = ((BASE_UTC + t) * 1000 + time_shift) % (604800 * 1000)
        recs = []
        used = 0
        for s in sats:
            elev = max(-90.0, min(90.0, s.elev0 + s.rate * t))
            if elev < 5:
                continue
            key = (s.gnss, s.sv)
            sigma = (1.0 + 2.0 * (1 - elev / 90)) * mp_scale
            mp[key] = 0.8 * mp[key] + rng.gauss(0, sigma * 0.6)
            if spoofing is not None and spoofing.kind == "spoof_smart":
                # power-matched spoofer that imitates the elevation profile
                cno = 28 + 0.22 * elev + spoofing.params["power_db"] + rng.gauss(0, 1.2)
            elif spoofing is not None:
                cno = 44.0 + spoofing.params.get("power_db", 5) * 0.5 + rng.gauss(0, 0.6)
            else:
                cno = 28 + 0.22 * elev + mp[key] - jam_db
                if blocked_sector is not None:
                    d = abs((s.azim - blocked_sector + 180) % 360 - 180)
                    cno = 0 if d < 90 else cno - 8
            cno = min(52.0, cno)
            if cno < 18:
                cno = 0
            used += 1 if cno >= 20 else 0
            flags = 0x0F if cno >= 20 else (0x04 if cno > 0 else 0x01)
            recs.append(struct.pack("<BBBbhhI", s.gnss, s.sv, int(round(cno)), int(round(elev)), int(s.azim),
                                    0, flags))
        sat_payload = struct.pack("<IBB2x", itow, 1, len(recs)) + b"".join(recs)

        # ---------------- navigation solution
        fix = 3 if used >= 5 else (2 if used == 4 else 0)
        if spoofing is not None and spoofing.kind == "spoof_drag":
            spoof_v = min(spoofing.params["vmax"], spoof_v + spoofing.params["accel"])
            hd = math.radians(spoofing.params["heading"])
            spoof_off_n += spoof_v * math.cos(hd)
            spoof_off_e += spoof_v * math.sin(hd)
        elif spoofing is not None and spoofing.kind == "spoof_jump":
            hd = math.radians(spoofing.params["heading"])
            dist = spoofing.params["dist_km"] * 1000
            spoof_off_n, spoof_off_e = dist * math.cos(hd), dist * math.sin(hd)
        elif spoofing is not None and spoofing.kind == "spoof_smart":
            hd = math.radians(spoofing.params["heading"])
            spoof_off_n += spoofing.params["speed"] * math.cos(hd)
            spoof_off_e += spoofing.params["speed"] * math.sin(hd)
        move_n = move_e = 0.0
        vel_n = vel_e = 0.0
        if ship:
            vel_n, vel_e = ship_speed * math.cos(ship_heading), ship_speed * math.sin(ship_heading)
            move_n, move_e = vel_n * t, vel_e * t
        lat = REF_LAT + (move_n + spoof_off_n + rng.gauss(0, 1.2)) / 111_195
        lon = REF_LON + (move_e + spoof_off_e + rng.gauss(0, 1.2)) / (111_195 * math.cos(math.radians(REF_LAT)))
        h = REF_H + rng.gauss(0, 2.0)
        hacc = 1500 + int(abs(rng.gauss(0, 400))) + int(jam_db * 120)
        spoof_state = 1
        if flags_on and spoofing is not None and t - spoofing.start > 20:
            spoof_state = 2

        status = struct.pack("<IBBBBII", itow, fix, 0x01 if fix >= 3 else 0, 0, (spoof_state & 3) << 3, 30000, t * 1000)
        rf_block = struct.pack("<BBBBI4sHHBbBbB3s", 0, jam_state & 3, 2, 1, 0, b"\x00" * 4,
                               int(max(0, min(65535, noise))), int(max(0, min(8191, agc))),
                               int(max(0, min(255, jam_ind))), 0, 0, 0, 0, b"\x00" * 3)
        mon_rf = struct.pack("<BB2x", 0, 1) + rf_block
        sec = BASE_UTC + t + time_shift // 1000
        nano = (time_shift % 1000) * 1_000_000
        tm = _utc_parts(sec)
        pvt = struct.pack("<IHBBBBBBIiBBBBiiiiIIiiiiiIIHH4xiHH",
                          itow, tm[0], tm[1], tm[2], tm[3], tm[4], tm[5], 0x07, 30, nano,
                          fix, 0x01 if fix >= 3 else 0, 0, used,
                          int(round(lon * 1e7)), int(round(lat * 1e7)), int(round(h * 1000)),
                          int(round((h - 25) * 1000)), hacc, hacc + 800,
                          int(round(vel_n * 1000)), int(round(vel_e * 1000)), 0,
                          int(round(math.hypot(vel_n, vel_e) * 1000)), 0, 1000, 5000, 150, 0, 0, 0, 0)
        out += ubx(0x01, 0x35, sat_payload)
        out += ubx(0x01, 0x03, status)
        out += ubx(0x0A, 0x38, mon_rf)
        out += ubx(0x01, 0x07, pvt)
    meta = {"scenario": name, "seed": seed, "duration": duration, "receiver_flags": flags_on,
            "platform": "ship" if ship else "static",
            "reference": [REF_LAT, REF_LON, REF_H], "truth": truth, "description": SCENARIOS[name]}
    return bytes(out), meta


def _utc_parts(sec: int) -> tuple[int, int, int, int, int, int]:
    import datetime as _dt
    d = _dt.datetime.fromtimestamp(sec, _dt.timezone.utc)
    return d.year, d.month, d.day, d.hour, d.minute, d.second


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("list")
    g = sub.add_parser("generate")
    g.add_argument("scenario", choices=list(SCENARIOS))
    g.add_argument("--seed", type=int, default=1)
    g.add_argument("--duration", type=int, default=900)
    g.add_argument("--receiver-flags", choices=["random", "on", "off"], default="random")
    g.add_argument("--out", type=Path, required=True)
    ga = sub.add_parser("generate-all")
    ga.add_argument("--out", type=Path, default=Path("data/scenarios"))
    ga.add_argument("--seed", type=int, default=1)
    a = ap.parse_args()
    if a.cmd == "list":
        for k, v in SCENARIOS.items():
            print(f"{k:16s} {v}")
        return
    if a.cmd == "generate":
        flags = None if a.receiver_flags == "random" else a.receiver_flags == "on"
        data, meta = generate(a.scenario, a.seed, a.duration, flags)
        a.out.parent.mkdir(parents=True, exist_ok=True)
        a.out.write_bytes(data)
        a.out.with_suffix(".truth.json").write_text(json.dumps(meta, indent=2))
        print(f"wrote {a.out} ({len(data)} bytes) + truth")
        return
    a.out.mkdir(parents=True, exist_ok=True)
    for name in SCENARIOS:
        data, meta = generate(name, a.seed, 900, False)  # receiver flags off: tests JamWarden's own logic
        (a.out / f"{name}.ubx").write_bytes(data)
        (a.out / f"{name}.truth.json").write_text(json.dumps(meta, indent=2))
        print(f"{name:16s} {len(data):>8} bytes  truth={meta['truth']}")


if __name__ == "__main__":
    main()
