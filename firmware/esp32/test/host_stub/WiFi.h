#pragma once
#define WIFI_STA 1
#define WL_CONNECTED 3
struct WiFiClass { void mode(int) {} void begin(const char*, const char*) {} int status() { return WL_CONNECTED; } };
extern WiFiClass WiFi;
