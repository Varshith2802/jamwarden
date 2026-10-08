// Host harness: links the firmware against the stubs and runs setup() + a few loop() iterations.
#include "Arduino.h"
#include "WiFi.h"
HardwareSerial Serial, Serial2;
WiFiClass WiFi;
void setup();
void loop();
int main() {
  setup();
  for (int i = 0; i < 3; ++i) loop();
  return 0;
}
