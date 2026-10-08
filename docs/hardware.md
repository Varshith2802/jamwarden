# JamWarden hardware guide

## Bill of materials (about 1 000 SEK)

| Part | Example | Notes |
|---|---|---|
| Microcontroller | ESP32 DevKit v1 (or ESP32-S3 DevKitC) | any ESP32 with a free UART |
| GNSS receiver | u-blox **NEO-M9N** or **MAX-M10S** breakout | best: MON-RF (AGC, noise, CW indicator) + built-in spoofing flag |
| (budget option) | u-blox NEO-M8N module | works through MON-HW; beware of clones with old firmware |
| Antenna | active L1 patch antenna with ground plane, SMA/u.FL | mount with a clear sky view, away from metal and LTE antennas |
| Optional | 5 V buzzer / LED, IP65 enclosure, USB or PoE power | |

## Wiring (ESP32 DevKit v1)

| GNSS module | ESP32 |
|---|---|
| TX | GPIO16 (RX2) |
| RX | GPIO17 (TX2) |
| VCC | 3V3 (check your breakout: many also accept 5 V) |
| GND | GND |
| (optional) buzzer + | GPIO set by `PIN_BUZZER` in `firmware/esp32/include/config.h` |

ESP32-S3: use the `esp32-s3` environment (GPIO18 = RX, GPIO17 = TX), or change the pins in `config.h`.

## Flashing

```bash
pip install platformio
cd firmware/esp32
pio run -e esp32dev -t upload          # or -e esp32dev-wifi after editing SSID/password/collector URL
pio device monitor                     # JSON lines at 115200 baud
```

On boot the firmware probes 38400/9600/115200/57600/19200 baud, enables the UBX messages it needs, turns NMEA off
and moves the link to 115200 baud. Configuration is written to RAM only, so the receiver returns to factory
settings after a power cycle and the firmware simply reconfigures it on the next boot.

Expected output:

```json
{"type":"boot","sensor":"jw-001","fw":"jamwarden-0.1.0"}
{"type":"health","msg":"receiver configured","baud":115200}
{"sensor":"jw-001","type":"status","epoch":1,"state":"learning", ...}
{"sensor":"jw-001","type":"event","event":"baseline_ready", ...}
```

The detector needs **60 seconds of clean sky** to learn its baseline. In static mode it also learns the
reference position then. To use a surveyed position instead, call `set_reference()` in `main.cpp`.

## Placement tips

* Static mode assumes the antenna does not move. Use `STATIC_MODE 0` on vehicles and vessels.
* Avoid metal roofs and walls nearby: multipath raises C/N0 variance (the detector tolerates this; see the
  `multipath` scenario) but lowers sensitivity.
* Indoor or obstructed antennas trigger nothing by design (`obstruction` scenario). They just see fewer
  satellites.

## Legal note

Transmitting GNSS jamming or spoofing signals is illegal in Sweden and the rest of the EU, including for
testing. Test JamWarden with the bundled simulator (`tools/scenarios.py`), with recorded UBX logs, or in a
shielded laboratory with the necessary permits. JamWarden itself is receive-only.
