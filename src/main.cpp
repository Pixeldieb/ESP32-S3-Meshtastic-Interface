#include <Arduino.h>
#include <Meshtastic.h>

#define MT_RX_PIN 44
#define MT_TX_PIN 43
#define MT_BAUD   115200

#define LED_PIN 21

const unsigned long SEND_INTERVAL_MS = 300000; // 5 Minuten = 5 * 60 * 1000
unsigned long lastSendTime = 0;

const unsigned long HEARTBEAT_INTERVAL_MS = 500;
unsigned long lastHeartbeatToggle = 0;
bool heartbeatState = false;

void ledOn()  { digitalWrite(LED_PIN, LOW); }
void ledOff() { digitalWrite(LED_PIN, HIGH); }

// Schnelles Blinken (fürs Senden und für den Erfolgs-Blitz)
void blinkFast(uint8_t times, uint16_t delayMs) {
  for (uint8_t i = 0; i < times; i++) {
    ledOn(); delay(delayMs);
    ledOff(); delay(delayMs);
  }
}

// Warte-Muster: kurzer Doppel-Blitz, dann Pause — klar unterscheidbar vom Heartbeat
void blinkWaiting() {
  ledOn(); delay(60);
  ledOff(); delay(80);
  ledOn(); delay(60);
  ledOff(); delay(600);
}

void setup() {
  Serial.begin(115200);
  delay(2000);

  pinMode(LED_PIN, OUTPUT);
  ledOff();

  Serial.println("Starte Meshtastic-Verbindung...");
  mt_serial_init(MT_RX_PIN, MT_TX_PIN, MT_BAUD);

  // --- Handshake: warten, bis Verbindung steht ---
  // LED blinkt im "Doppel-Blitz"-Muster, solange nicht verbunden
  bool connected = false;
  while (!connected) {
    connected = mt_loop(millis());
    if (!connected) {
      blinkWaiting();
    }
  }

  Serial.println(">>> VERBUNDEN mit der Node");

  // --- Erfolg: 5x schnell blinken ---
  blinkFast(5, 100);

  lastHeartbeatToggle = millis();
}

void loop() {
  bool connected = mt_loop(millis());

  // Verbindung mittendrin verloren -> zurück ins Warte-Muster, bis sie wieder da ist
  if (!connected) {
    blinkWaiting();
    return;
  }

  if (millis() - lastSendTime >= SEND_INTERVAL_MS) {
    lastSendTime = millis();
    blinkFast(6, 80);

    bool ok = mt_send_text("Interface Test Kayna-Funkt 5min Intervall");
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