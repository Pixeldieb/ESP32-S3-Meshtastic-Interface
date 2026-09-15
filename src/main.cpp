hello vibe coders, does this commit fine?
#include <Arduino.h>
#include <Meshtastic.h>

// --- Pin-Definitionen ---
#define MT_RX_PIN 44      // D7 — Daten von der Node
#define MT_TX_PIN 43      // D6 — Daten zur Node
#define MT_BAUD   115200

#define LED_PIN 21        // eingebaute LED — aktiv LOW (LOW = an)

// --- Timing: Senden ---
const unsigned long SEND_INTERVAL_MS = 20000; // 20 Sekunden
unsigned long lastSendTime = 0;

// --- Timing: Heartbeat (langsames Blinken im Ruhezustand) ---
const unsigned long HEARTBEAT_INTERVAL_MS = 500;
unsigned long lastHeartbeatToggle = 0;
bool heartbeatState = false;

void ledOn()  { digitalWrite(LED_PIN, LOW); }
void ledOff() { digitalWrite(LED_PIN, HIGH); }

// Schnelles Blinken beim Senden — kurz, blockierend ist hier ok
void blinkFast(uint8_t times, uint16_t delayMs) {
  for (uint8_t i = 0; i < times; i++) {
    ledOn();
    delay(delayMs);
    ledOff();
    delay(delayMs);
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(LED_PIN, OUTPUT);
  ledOff();

  mt_serial_init(MT_RX_PIN, MT_TX_PIN, MT_BAUD);
}

void loop() {
  bool connected = mt_loop(millis());

  // --- Senden alle 20 Sekunden, LED blinkt dabei schnell ---
  if (connected && millis() - lastSendTime >= SEND_INTERVAL_MS) {
    lastSendTime = millis();
    blinkFast(6, 80);
    mt_send_text("Hallo von der ESP32-S3!");
    lastHeartbeatToggle = millis(); // Heartbeat-Takt nach dem Senden neu synchronisieren
  }

  // --- Heartbeat: langsames Blinken, solange nicht gesendet wird ---
  if (millis() - lastHeartbeatToggle >= HEARTBEAT_INTERVAL_MS) {
    lastHeartbeatToggle = millis();
    heartbeatState = !heartbeatState;
    heartbeatState ? ledOn() : ledOff();
  }
}