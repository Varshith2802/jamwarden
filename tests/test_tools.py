"""Python-side tests: scenario generator, evaluation helpers, collector API, and end-to-end replay."""
import json
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "dashboard"))
sys.path.insert(0, str(ROOT / "eval"))

from scenarios import SCENARIOS, generate  # noqa: E402

REPLAY = ROOT / "core" / "build" / "jamwarden_replay"


def parse_frames(data: bytes):
    i, out = 0, []
    while i < len(data):
        assert data[i:i + 2] == b"\xb5\x62", f"sync lost at {i}"
        cls, mid, ln = data[i + 2], data[i + 3], int.from_bytes(data[i + 4:i + 6], "little")
        body = data[i + 2:i + 6 + ln]
        a = b = 0
        for x in body:
            a = (a + x) & 0xFF
            b = (b + a) & 0xFF
        assert data[i + 6 + ln] == a and data[i + 7 + ln] == b, "bad checksum"
        out.append((cls, mid, ln))
        i += 8 + ln
    return out


@pytest.mark.parametrize("name", list(SCENARIOS))
def test_generator_produces_valid_ubx(name):
    data, meta = generate(name, seed=3, duration=120)
    frames = parse_frames(data)
    assert len(frames) == 4 * 120
    assert {(c, m) for c, m, _ in frames} == {(0x01, 0x35), (0x01, 0x03), (0x0A, 0x38), (0x01, 0x07)}
    assert all(ln == 92 for c, m, ln in frames if (c, m) == (0x01, 0x07))
    assert meta["scenario"] == name and isinstance(meta["truth"], list)


def test_generator_is_deterministic():
    assert generate("jam_cw", 5, 60)[0] == generate("jam_cw", 5, 60)[0]
    assert generate("jam_cw", 5, 60)[0] != generate("jam_cw", 6, 60)[0]


def test_flags_only_baseline():
    from run_eval import flags_only_detect
    status = [{"epoch": e, "ubx_jam": 2 if 105 <= e <= 110 else 1, "ubx_spoof": 1} for e in range(100, 120)]
    assert flags_only_detect(status, "jamming", 100, 115) == 107
    assert flags_only_detect(status, "spoofing", 100, 115) is None


def test_collector_api(tmp_path):
    from fastapi.testclient import TestClient
    from collector import Store, create_app
    store = Store(tmp_path / "jw.db")
    store.ingest('{"type":"status","sensor":"s1","epoch":1,"state":"ok","lat":56.1,"lon":15.5,"cno_top":45}')
    store.ingest({"type": "event", "sensor": "s1", "event": "jamming_start", "severity": "warning",
                  "lat": 56.1, "lon": 15.5, "reasons": ["agc_anomaly"]})
    assert store.ingest("not json") is None
    c = TestClient(create_app(store))
    assert c.get("/api/sensors").json()[0]["state"] == "ok"
    assert c.get("/api/events").json()[0]["event"] == "jamming_start"
    assert c.post("/api/ingest", content='{"type":"status","sensor":"s2","epoch":5,"state":"jamming"}').json() == {"ingested": 1}
    assert len(c.get("/api/sensors").json()) == 2
    gj = c.get("/api/events.geojson").json()
    assert gj["features"][0]["geometry"]["coordinates"] == [15.5, 56.1]
    assert "JamWarden" in c.get("/").text
    assert c.get("/static/leaflet/leaflet.js").status_code == 200


@pytest.mark.skipif(not REPLAY.exists(), reason="build core first (cmake -S core -B core/build && cmake --build core/build)")
@pytest.mark.parametrize("name,expect", [("clean", []), ("jam_broadband", ["jamming_start"]),
                                         ("spoof_jump", ["spoofing_start"]), ("obstruction", [])])
def test_end_to_end_replay(tmp_path, name, expect):
    data, meta = generate(name, seed=11, duration=600, ubx_flags=False)
    f = tmp_path / "s.ubx"
    f.write_bytes(data)
    out = subprocess.run([str(REPLAY), "--events-only", str(f)], capture_output=True, text=True, check=True).stdout
    events = [json.loads(l) for l in out.splitlines() if '"type":"event"' in l]
    starts = [e["event"] for e in events if e["event"].endswith("_start")]
    assert starts == expect
