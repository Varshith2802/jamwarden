// JamWarden firmware for ESP32 + u-blox M8/M9/M10 receiver.
//
// - auto-detects the receiver baud rate, enables NAV-PVT, NAV-SAT, NAV-STATUS, MON-RF (M9/M10) or MON-HW (M8)
//   and NAV-EOE, disables NMEA and moves the link to 115200 baud
// - runs the portable JamWarden detector on every navigation epoch
// - prints JSON lines on USB serial (status + events), drives an alarm LED / buzzer
// - optionally uploads events and periodic status to the dashboard collector over Wi-Fi
#include <Arduino.h>

#include "config.h"
#include "jamwarden/detector.hpp"
#include "jamwarden/ubx.hpp"

#if JW_WIFI
#include <HTTPClient.h>
#include <WiFi.h>
#endif

static HardwareSerial& GNSS = Serial2;
static jw::UbxParser parser;
static jw::Detector* detector = nullptr;
static uint32_t last_epoch_ms = 0, last_health_ms = 0;
#if JW_WIFI
static uint32_t last_upload_ms = 0;
#endif
static uint32_t epochs = 0;

// ------------------------------------------------------------------------------------- receiver setup
static void send_ubx(uint8_t cls, uint8_t id, const uint8_t* payload, uint16_t len) {
  uint8_t buf[160];
  const size_t n = jw::ubx_encode(cls, id, payload, len, buf, sizeof(buf));
  if (n) {
    GNSS.write(buf, n);
    GNSS.flush();
  }
}

struct Probe {
  bool ubx = false;
  int nmea = 0;
  uint8_t prev = 0;
};

static bool probe_baud(uint32_t baud) {
  GNSS.end();
  GNSS.setRxBufferSize(4096);
  GNSS.begin(baud, SERIAL_8N1, PIN_GNSS_RX, PIN_GNSS_TX);
  delay(150);
  while (GNSS.available()) GNSS.read();
  send_ubx(jw::cls::MON, jw::id::MON_VER, nullptr, 0);
  Probe pr;
  jw::UbxParser p;
  p.set_callback([](const jw::UbxFrame&, void* c) { static_cast<Probe*>(c)->ubx = true; }, &pr);
  const uint32_t t0 = millis();
  while (millis() - t0 < 1500 && !pr.ubx && pr.nmea < 3) {
    while (GNSS.available()) {
      const uint8_t b = static_cast<uint8_t>(GNSS.read());
      p.feed(b);
      if (pr.prev == '$' && b == 'G') ++pr.nmea;  // "$GP...", "$GN..."
      pr.prev = b;
    }
    delay(2);
  }
  return pr.ubx || pr.nmea >= 3;
}

static void valset_u8(uint8_t* p, size_t& n, uint32_t key, uint8_t val) {
  for (int i = 0; i < 4; ++i) p[n++] = static_cast<uint8_t>(key >> (8 * i));
  p[n++] = val;
}

static void configure_receiver() {
  // M9 / M10: configuration interface (CFG-VALSET, RAM layer)
  uint8_t p[96];
  size_t n = 0;
  p[n++] = 0x00;  // version
  p[n++] = 0x01;  // layers: RAM
  p[n++] = 0x00;
  p[n++] = 0x00;
  valset_u8(p, n, 0x20910007, 1);  // CFG-MSGOUT-UBX_NAV_PVT_UART1
  valset_u8(p, n, 0x20910016, 1);  // CFG-MSGOUT-UBX_NAV_SAT_UART1
  valset_u8(p, n, 0x2091001b, 1);  // CFG-MSGOUT-UBX_NAV_STATUS_UART1
  valset_u8(p, n, 0x2091035a, 1);  // CFG-MSGOUT-UBX_MON_RF_UART1
  valset_u8(p, n, 0x20910160, 1);  // CFG-MSGOUT-UBX_NAV_EOE_UART1
  valset_u8(p, n, 0x10740002, 0);  // CFG-UART1OUTPROT-NMEA = off
  send_ubx(jw::cls::CFG, jw::id::CFG_VALSET, p, static_cast<uint16_t>(n));
  delay(100);
  // M8: legacy CFG-MSG (class, id, rate on the current port). Unknown messages are simply NAK'ed.
  const uint8_t msgs[][2] = {{0x01, 0x07}, {0x01, 0x35}, {0x01, 0x03}, {0x0A, 0x09}, {0x01, 0x61}};
  for (auto& m : msgs) {
    const uint8_t pl[3] = {m[0], m[1], 1};
    send_ubx(jw::cls::CFG, jw::id::CFG_MSG, pl, 3);
    delay(30);
  }
}

static uint32_t switch_to_115200(uint32_t current) {
  if (current == 115200) return current;
  // M9/M10: CFG-UART1-BAUDRATE (U4)
  uint8_t p[12] = {0x00, 0x01, 0x00, 0x00, 0x01, 0x00, 0x52, 0x40, 0x00, 0xC2, 0x01, 0x00};  // key 0x40520001
  send_ubx(jw::cls::CFG, jw::id::CFG_VALSET, p, sizeof(p));
  // M8: CFG-PRT for UART1, 8N1, 115200, in: UBX+NMEA+RTCM, out: UBX only
  uint8_t prt[20] = {0};
  prt[0] = 1;                                  // portID UART1
  prt[4] = 0xD0; prt[5] = 0x08;                // mode 8N1 (0x000008D0)
  prt[8] = 0x00; prt[9] = 0xC2; prt[10] = 0x01;  // 115200
  prt[12] = 0x07;                              // inProtoMask
  prt[14] = 0x01;                              // outProtoMask: UBX
  send_ubx(jw::cls::CFG, jw::id::CFG_PRT, prt, sizeof(prt));
  delay(200);
  if (probe_baud(115200)) return 115200;
  probe_baud(current);  // receiver refused: stay at the old rate
  return current;
}

// ------------------------------------------------------------------------------------- output
static void set_alarm(bool on) {
  if (PIN_ALARM_LED >= 0) digitalWrite(PIN_ALARM_LED, on ? HIGH : LOW);
  if (PIN_BUZZER >= 0) digitalWrite(PIN_BUZZER, on ? HIGH : LOW);
}

static String with_sensor(const std::string& json) {
  // insert "sensor":"id" after the opening brace
  String s = "{\"sensor\":\"" SENSOR_ID "\",";
  s += (json.c_str() + 1);
  return s;
}

#if JW_WIFI
static void upload(const String& line) {
  if (WiFi.status() != WL_CONNECTED) return;
  HTTPClient http;
  http.setTimeout(1500);
  if (http.begin(COLLECTOR_URL)) {
    http.addHeader("Content-Type", "application/json");
    http.POST(line);
    http.end();
  }
}
#endif

static void on_frame(const jw::UbxFrame& f, void*) {
  detector->on_frame(f, millis());
  jw::Event e;
  while (detector->poll_event(e)) {
    const String line = with_sensor(jw::Detector::event_json(e));
    Serial.println(line);
#if JW_WIFI
    upload(line);
#endif
  }
  if (detector->epoch_ready()) {
    ++epochs;
    last_epoch_ms = millis();
    const jw::EpochMetrics& m = detector->last();
    set_alarm(m.jamming || m.spoofing);
    if (epochs % STATUS_EVERY == 0) {
      const String line = with_sensor(detector->status_json());
      Serial.println(line);
#if JW_WIFI
      if (millis() - last_upload_ms >= UPLOAD_STATUS_EVERY_S * 1000UL) {
        last_upload_ms = millis();
        upload(line);
      }
#endif
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);
  if (PIN_ALARM_LED >= 0) pinMode(PIN_ALARM_LED, OUTPUT);
  if (PIN_BUZZER >= 0) pinMode(PIN_BUZZER, OUTPUT);
  set_alarm(true);
  Serial.println("{\"type\":\"boot\",\"sensor\":\"" SENSOR_ID "\",\"fw\":\"jamwarden-0.1.0\"}");

  static const uint32_t bauds[] = {38400, 9600, 115200, 57600, 19200};
  uint32_t found = 0;
  for (uint32_t b : bauds) {
    if (probe_baud(b)) {
      found = b;
      break;
    }
  }
  if (!found) {
    Serial.println("{\"type\":\"health\",\"msg\":\"no GNSS receiver detected - check wiring/power\"}");
    found = 38400;
    probe_baud(found);
  }
  configure_receiver();
  const uint32_t baud = switch_to_115200(found);
  Serial.printf("{\"type\":\"health\",\"msg\":\"receiver configured\",\"baud\":%lu}\n",
                static_cast<unsigned long>(baud));

  jw::DetectorConfig cfg;
  cfg.static_mode = STATIC_MODE != 0;
  detector = new jw::Detector(cfg);
  parser.set_callback(on_frame, nullptr);

#if JW_WIFI
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
#endif
  set_alarm(false);
  last_epoch_ms = millis();
}

void loop() {
  while (GNSS.available()) parser.feed(static_cast<uint8_t>(GNSS.read()));
  const uint32_t now = millis();
  if (now - last_epoch_ms > 5000 && now - last_health_ms > 5000) {
    last_health_ms = now;
    Serial.printf("{\"type\":\"health\",\"sensor\":\"%s\",\"msg\":\"no navigation epochs for %lu s\",\"frames\":%lu,"
                  "\"checksum_errors\":%lu}\n",
                  SENSOR_ID, static_cast<unsigned long>((now - last_epoch_ms) / 1000),
                  static_cast<unsigned long>(parser.frames()), static_cast<unsigned long>(parser.checksum_errors()));
  }
  delay(1);
}
