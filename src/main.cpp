#include <Arduino.h>
#include <Meshtastic.h>

#define MT_RX_PIN 44
#define MT_TX_PIN 43
#define MT_BAUD   115200

#define LED_PIN 21

const unsigned long SEND_INTERVAL_MS = 20000;
unsigned long lastSendTime = 0;

const unsigned long HEARTBEAT_INTERVAL_MS = 500;
unsigned long lastHeartbeatToggle = 0;
bool heartbeatState = false;

bool wasConnected = false;

void ledOn()  { digitalWrite(LED_PIN, LOW); }
void ledOff() { digitalWrite(LED_PIN, HIGH); }

void blinkFast(uint8_t times, uint16_t delayMs) {
  for (uint8_t i = 0; i < times; i++) {
    ledOn(); delay(delayMs);
    ledOff(); delay(delayMs);
  }
}

void setup() {
  Serial.begin(115200);
  delay(2000);

  pinMode(LED_PIN, OUTPUT);
  ledOff();

  Serial.println("Starte Meshtastic-Verbindung...");
  mt_set_debug(true);

  mt_serial_init(MT_RX_PIN, MT_TX_PIN, MT_BAUD);
}

void loop() {
  bool connected = mt_loop(millis());

  if (connected != wasConnected) {
    wasConnected = connected;
    Serial.println(connected ? ">>> VERBUNDEN mit der Node" : ">>> Verbindung verloren/noch nicht da");
  }

  if (connected && millis() - lastSendTime >= SEND_INTERVAL_MS) {
    lastSendTime = millis();
    blinkFast(6, 80);

    bool ok = mt_send_text("Hallo von der ESP32-S3!");
    Serial.print(">>> Nachricht gesendet, Erfolg: ");
    Serial.println(ok ? "JA" : "NEIN");

    lastHeartbeatToggle = millis();
  }

  if (millis() - lastHeartbeatToggle >= HEARTBEAT_INTERVAL_MS) {
    lastHeartbeatToggle = millis();
    heartbeatState = !heartbeatState;
    heartbeatState ? ledOn() : ledOff();
  }
}