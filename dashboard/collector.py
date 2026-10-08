#!/usr/bin/env python3
"""JamWarden collector + live interference map.

Ingests JSON lines from sensors and serves a dashboard (map, charts, event log) on http://localhost:8050

    python dashboard/collector.py --serial /dev/ttyUSB0          # ESP32 on USB (Windows: COM3)
    python dashboard/collector.py --file capture.jsonl           # import a recorded session
    ./core/build/jamwarden_replay data/scenarios/jam_then_spoof.ubx | python dashboard/collector.py --stdin
    # Wi-Fi sensors POST to http://<this-host>:8050/api/ingest

Endpoints: GET /api/sensors  /api/events  /api/series?sensor=ID  /api/events.geojson   POST /api/ingest
"""
from __future__ import annotations

import argparse
import json
import sqlite3
import sys
import threading
import time
from pathlib import Path

from fastapi import FastAPI, Request
from fastapi.responses import HTMLResponse, JSONResponse
from fastapi.staticfiles import StaticFiles

HERE = Path(__file__).resolve().parent
SCHEMA = """
CREATE TABLE IF NOT EXISTS status (id INTEGER PRIMARY KEY, sensor TEXT, received REAL, utc TEXT, epoch INTEGER,
  state TEXT, lat REAL, lon REAL, cno_top REAL, agc INTEGER, noise INTEGER, jamInd INTEGER, jam_score REAL,
  spoof_score REAL, raw TEXT);
CREATE INDEX IF NOT EXISTS status_sensor ON status(sensor, id);
CREATE TABLE IF NOT EXISTS events (id INTEGER PRIMARY KEY, sensor TEXT, received REAL, utc TEXT, event TEXT,
  severity TEXT, lat REAL, lon REAL, reasons TEXT, raw TEXT);
"""


class Store:
    def __init__(self, path: Path):
        self.lock = threading.Lock()
        self.db = sqlite3.connect(str(path), check_same_thread=False)
        self.db.executescript(SCHEMA)

    def ingest(self, line: str | dict, default_sensor: str = "local") -> str | None:
        try:
            d = json.loads(line) if isinstance(line, str) else line
        except ValueError:
            return None
        if not isinstance(d, dict):
            return None
        sensor = str(d.get("sensor", default_sensor))
        now = time.time()
        with self.lock:
            if d.get("type") == "status":
                self.db.execute(
                    "INSERT INTO status(sensor,received,utc,epoch,state,lat,lon,cno_top,agc,noise,jamInd,jam_score,"
                    "spoof_score,raw) VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
                    (sensor, now, d.get("utc"), d.get("epoch"), d.get("state"), d.get("lat"), d.get("lon"),
                     d.get("cno_top"), d.get("agc"), d.get("noise"), d.get("jamInd"), d.get("jam_score"),
                     d.get("spoof_score"), json.dumps(d)))
            elif d.get("type") == "event":
                self.db.execute(
                    "INSERT INTO events(sensor,received,utc,event,severity,lat,lon,reasons,raw) VALUES (?,?,?,?,?,?,?,?,?)",
                    (sensor, now, d.get("utc"), d.get("event"), d.get("severity"), d.get("lat"), d.get("lon"),
                     json.dumps(d.get("reasons", [])), json.dumps(d)))
            else:
                return d.get("type")
            self.db.commit()
        return d.get("type")

    def query(self, sql: str, args: tuple = ()) -> list[dict]:
        with self.lock:
            cur = self.db.execute(sql, args)
            cols = [c[0] for c in cur.description]
            return [dict(zip(cols, r)) for r in cur.fetchall()]


def create_app(store: Store) -> FastAPI:
    app = FastAPI(title="JamWarden collector")
    app.mount("/static", StaticFiles(directory=str(HERE / "static")), name="static")

    @app.get("/", response_class=HTMLResponse)
    def index() -> str:
        return (HERE / "index.html").read_text(encoding="utf-8")

    @app.post("/api/ingest")
    async def ingest(request: Request) -> dict:
        body = (await request.body()).decode("utf-8", "replace")
        n = sum(1 for line in body.splitlines() if line.strip() and store.ingest(line, "wifi"))
        return {"ingested": n}

    @app.get("/api/sensors")
    def sensors() -> list[dict]:
        return store.query("SELECT s.* FROM status s JOIN (SELECT sensor, MAX(id) mid FROM status GROUP BY sensor) m "
                           "ON s.id = m.mid ORDER BY s.sensor")

    @app.get("/api/events")
    def events(limit: int = 200) -> list[dict]:
        return store.query("SELECT id,sensor,utc,event,severity,lat,lon,reasons FROM events ORDER BY id DESC LIMIT ?",
                           (limit,))

    @app.get("/api/series")
    def series(sensor: str | None = None, n: int = 900) -> list[dict]:
        if sensor is None:
            row = store.query("SELECT sensor FROM status ORDER BY id DESC LIMIT 1")
            if not row:
                return []
            sensor = row[0]["sensor"]
        rows = store.query("SELECT epoch,utc,state,cno_top,agc,noise,jamInd,jam_score,spoof_score FROM status "
                           "WHERE sensor=? ORDER BY id DESC LIMIT ?", (sensor, n))
        return rows[::-1]

    @app.get("/api/events.geojson")
    def geojson() -> JSONResponse:
        feats = [{"type": "Feature", "geometry": {"type": "Point", "coordinates": [e["lon"], e["lat"]]},
                  "properties": {k: e[k] for k in ("sensor", "utc", "event", "severity", "reasons")}}
                 for e in store.query("SELECT * FROM events WHERE event LIKE '%_start' AND lat IS NOT NULL")]
        return JSONResponse({"type": "FeatureCollection", "features": feats})

    return app


def _pump(lines, store: Store, sensor: str) -> None:
    for line in lines:
        line = line.decode("utf-8", "replace") if isinstance(line, bytes) else line
        line = line.strip()
        if line.startswith("{"):
            store.ingest(line, sensor)


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    src = ap.add_mutually_exclusive_group()
    src.add_argument("--serial", help="serial port of a USB-connected sensor")
    src.add_argument("--stdin", action="store_true")
    src.add_argument("--file", type=Path)
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--sensor", default="local", help="sensor id for lines without one")
    ap.add_argument("--db", type=Path, default=Path("jamwarden.db"))
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=8050)
    ap.add_argument("--no-server", action="store_true", help="only import (with --file/--stdin)")
    a = ap.parse_args()
    store = Store(a.db)
    if a.file:
        with a.file.open() as f:
            _pump(f, store, a.sensor)
        print(f"imported {a.file} into {a.db}")
    elif a.stdin:
        threading.Thread(target=_pump, args=(sys.stdin, store, a.sensor), daemon=True).start()
    elif a.serial:
        import serial  # pyserial

        def read_serial() -> None:
            while True:
                try:
                    with serial.Serial(a.serial, a.baud, timeout=1) as port:
                        print(f"reading {a.serial} @ {a.baud}")
                        _pump(iter(port.readline, None), store, a.sensor)
                except serial.SerialException as e:
                    print(f"serial error: {e}; retrying in 3 s")
                    time.sleep(3)
        threading.Thread(target=read_serial, daemon=True).start()
    if a.no_server:
        if a.stdin:
            _pump(sys.stdin, store, a.sensor)
        return
    import uvicorn
    print(f"dashboard: http://{a.host}:{a.port}")
    uvicorn.run(create_app(store), host=a.host, port=a.port, log_level="warning")


if __name__ == "__main__":
    main()
