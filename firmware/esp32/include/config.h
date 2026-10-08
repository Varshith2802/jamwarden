// JamWarden firmware configuration. Edit, then: pio run -t upload && pio device monitor
#pragma once

// ---- wiring (ESP32 DevKit v1): GNSS TX -> GPIO16 (RX2), GNSS RX <- GPIO17 (TX2), 3V3, GND
#ifndef PIN_GNSS_RX
#define PIN_GNSS_RX 16
#endif
#ifndef PIN_GNSS_TX
#define PIN_GNSS_TX 17
#endif
#ifndef PIN_ALARM_LED
#define PIN_ALARM_LED 2          // on-board LED on most DevKits
#endif
#ifndef PIN_BUZZER
#define PIN_BUZZER -1            // optional active buzzer, -1 = none
#endif

// ---- sensor
#ifndef SENSOR_ID
#define SENSOR_ID "jw-001"
#endif
#ifndef STATIC_MODE
#define STATIC_MODE 1            // 1 = fixed installation (roof, mast, crane), 0 = vehicle / vessel
#endif
#ifndef STATUS_EVERY
#define STATUS_EVERY 1           // print a status line every N epochs (1 Hz)
#endif

// ---- optional Wi-Fi uplink to the dashboard collector (dashboard/collector.py)
#ifndef JW_WIFI
#define JW_WIFI 0
#endif
#ifndef WIFI_SSID
#define WIFI_SSID "your-ssid"
#endif
#ifndef WIFI_PASS
#define WIFI_PASS "your-password"
#endif
#ifndef COLLECTOR_URL
#define COLLECTOR_URL "http://192.168.1.10:8050/api/ingest"
#endif
#ifndef UPLOAD_STATUS_EVERY_S
#define UPLOAD_STATUS_EVERY_S 10 // events are uploaded immediately, status every N seconds
#endif
