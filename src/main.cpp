#include <Arduino.h>
#include <Meshtastic.h>
#include "lage_db.h"

#define MT_RX_PIN 44
#define MT_TX_PIN 43
#define MT_BAUD   115200

#define LED_PIN 21

const unsigned long SEND_INTERVAL_MS = 300000; // 5 Minuten = 5 * 60 * 1000
unsigned long lastSendTime = 0;

const unsigned long HEARTBEAT_INTERVAL_MS = 500;
unsigned long lastHeartbeatToggle = 0;
bool heartbeatState = false;

// --- Zustandsmodell der Säule (Issue #10) ---
enum class SaeulenZustand {
  STANDBY,
  AKTIV,
  STROMAUSFALL,
  WARTUNG,
  SABOTAGE
};

SaeulenZustand aktuellerZustand = SaeulenZustand::STANDBY;

const char* zustandName(SaeulenZustand z) {
  switch (z) {
    case SaeulenZustand::STANDBY:      return "STANDBY";
    case SaeulenZustand::AKTIV:        return "AKTIV";
    case SaeulenZustand::STROMAUSFALL: return "STROMAUSFALL";
    case SaeulenZustand::WARTUNG:      return "WARTUNG";
    case SaeulenZustand::SABOTAGE:     return "SABOTAGE";
  }
  return "UNBEKANNT";
}

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

// --- Callback: wird von mt_loop() aufgerufen, wenn eine Textnachricht ankommt ---
void onTextMessage(uint32_t from, uint32_t to, uint8_t channel, const char* text) {
  String msg(text);
  if (!msg.startsWith("LAGE:")) return; // alles andere ignorieren

  String payload = msg.substring(5);
  int p1 = payload.indexOf(';');
  int p2 = payload.indexOf(';', p1 + 1);
  int p3 = payload.indexOf(';', p2 + 1);
  if (p1 < 0 || p2 < 0 || p3 < 0) {
    Serial.println("Ungueltiges LAGE-Format, erwartet: LAGE:<ID|NEU>;Kategorie;Status;Text");
    return;
  }

  String idPart = payload.substring(0, p1); idPart.trim();
  String kategorie = payload.substring(p1 + 1, p2); kategorie.trim();
  String status = payload.substring(p2 + 1, p3); status.trim();
  String content = payload.substring(p3 + 1); content.trim();

  char fromBuf[12];
  snprintf(fromBuf, sizeof(fromBuf), "!%08x", from);
  String fromNode(fromBuf);

  if (idPart.equalsIgnoreCase("NEU")) {
    int newId = lageDbCreate(kategorie, status, content, fromNode);
    Serial.print(">>> Neue Lagemeldung angelegt, ID "); Serial.println(newId);
  } else {
    int id = idPart.toInt();
    if (lageDbUpdate(id, kategorie, status, content, fromNode)) {
      Serial.print(">>> Lagemeldung "); Serial.print(id); Serial.println(" aktualisiert");
    } else {
      Serial.print(">>> Update fehlgeschlagen, ID "); Serial.print(id); Serial.println(" nicht gefunden");
    }
  }
  blinkFast(3, 100);
}

// --- Serial-CLI zum Prüfen der Datenbank ---
void handleSerialCommand(const String& cmd) {
  if (cmd == "liste") {
    lageDbListSummary();
  } else if (cmd.startsWith("liste kategorie ")) {
    lageDbListSummary(cmd.substring(16), "");
  } else if (cmd.startsWith("liste status ")) {
    lageDbListSummary("", cmd.substring(13));
  } else if (cmd.startsWith("detail ")) {
    lageDbShowDetail(cmd.substring(7).toInt());
  } else if (cmd == "status") {
    Serial.print("Zustand: "); Serial.println(zustandName(aktuellerZustand));
  } else if (cmd == "help") {
    Serial.println("Befehle: liste | liste kategorie <X> | liste status <X> | detail <ID> | status | help");
  } else if (cmd.length() > 0) {
    Serial.println("Unbekannter Befehl. 'help' fuer Uebersicht.");
  }
}

void setup() {
  Serial.begin(115200);
  delay(2000);

  pinMode(LED_PIN, OUTPUT);
  ledOff();

  lageDbBegin();

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

  set_text_message_callback(onTextMessage); // NACH erfolgtem Handshake registrieren

  Serial.println(">>> VERBUNDEN mit der Node. Tippe 'help' fuer CLI-Befehle.");

  // --- Erfolg: 5x schnell blinken ---
  blinkFast(5, 100);

  lastHeartbeatToggle = millis();
}

void loop() {
  bool connected = mt_loop(millis()); // ruft bei Bedarf onTextMessage() auf

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

  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    handleSerialCommand(cmd);
  }
}