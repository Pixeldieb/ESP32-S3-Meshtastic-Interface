#include "meshtastic_bridge.h"

#include <Meshtastic.h>

#include "lage_db.h"
#include "ui_model.h"

// Confirmed free on this board (see display_profiles/sensecap_indicator_d1l.h
// for everything that's NOT free): 0-21 (LCD), 39-41/48 (I2C+ST7701 SPI),
// 45 (backlight), 42 (IO expander INT), 43/44 (console UART), 38 (button).
#define MT_TX_PIN 22
#define MT_RX_PIN 23
#define MT_BAUD 115200

namespace {

// mt_loop()'s own return value is not a reliable "handshake done" signal
// (the library always returns true from the underlying serial loop —
// see src/xiao/main.cpp's onNodeReport comment for the same finding on
// the other board). Track real completion via the node-report callback
// instead, exactly like that board does.
bool g_initialized = false; // true only once meshtastic_bridge_begin() actually ran
bool g_config_done = false;
bool g_reported_connected = false;

void onNodeReport(mt_node_t *node, mt_nr_progress_t progress) {
  if (progress == MT_NR_DONE || progress == MT_NR_INVALID) {
    g_config_done = true;
  }
}

// Same LAGE:<ID|NEU>;Kategorie;Status;Text format src/xiao/main.cpp
// parses (see main README "Lagemeldungen (kayna-funkt)"). State
// short-codes (LGE/NOR/WTG/SAB/SAUS, that board's "Zustandsmodell der
// Saeule") are NOT handled here yet — this board doesn't have that
// state concept wired into its UI at all so far; only lage_db entries.
void onTextMessage(uint32_t from, uint32_t to, uint8_t channel, const char *text) {
  ui_model_notify_rx();
  String msg(text);
  Serial.printf("[MESH] von !%08x: \"%s\"\n", (unsigned)from, msg.c_str());

  if (!msg.startsWith("LAGE:")) return;

  String payload = msg.substring(5);
  int p1 = payload.indexOf(';');
  int p2 = payload.indexOf(';', p1 + 1);
  int p3 = payload.indexOf(';', p2 + 1);
  if (p1 < 0 || p2 < 0 || p3 < 0) {
    Serial.println("[MESH] Ungueltiges LAGE-Format");
    return;
  }

  String idPart = payload.substring(0, p1); idPart.trim();
  String kategorie = payload.substring(p1 + 1, p2); kategorie.trim();
  String status = payload.substring(p2 + 1, p3); status.trim();
  String content = payload.substring(p3 + 1); content.trim();

  char fromBuf[12];
  snprintf(fromBuf, sizeof(fromBuf), "!%08x", (unsigned)from);
  String fromNode(fromBuf);

  if (idPart.equalsIgnoreCase("NEU")) {
    int id = lageDbCreate(kategorie, status, content, fromNode);
    Serial.printf("[MESH] Neue Lagemeldung angelegt, ID %d\n", id);
  } else {
    bool ok = lageDbUpdate(idPart.toInt(), kategorie, status, content, fromNode);
    Serial.printf("[MESH] Lagemeldung %d %s\n", idPart.toInt(), ok ? "aktualisiert" : "nicht gefunden");
  }
}

} // namespace

void meshtastic_bridge_begin() {
  Serial.println("[MESH] Starte Verbindungsaufbau zur Meshtastic-Node...");
  mt_serial_init(MT_RX_PIN, MT_TX_PIN, MT_BAUD);
  // Kicks off the real want_config handshake; onNodeReport fires once
  // it's actually done. Deliberately non-blocking here (unlike the XIAO
  // board's setup(), which busy-waits) — this board has a screen that
  // should show up immediately, connected or not.
  mt_request_node_report(onNodeReport);
  g_initialized = true;
}

void meshtastic_bridge_loop() {
  if (!g_initialized) return; // see meshtastic_send_emergency's comment
  mt_loop(millis()); // must run every loop: drives the handshake + incoming messages

  if (g_config_done && !g_reported_connected) {
    g_reported_connected = true;
    // Registered only after handshake completion, same as src/xiao/main.cpp
    // (comment there: registering earlier meant the node never actually
    // forwarded mesh messages to the ESP32).
    set_text_message_callback(onTextMessage);
    ui_model_set_connected(true);
    Serial.println("[MESH] Verbunden.");
  }
}

bool meshtastic_send_emergency(const char *category, const char *type, const char *label) {
  // Calling mt_send_text() before mt_serial_init() ever ran (currently
  // true whenever meshtastic_bridge_begin() isn't called — see the note
  // in main.cpp's setup(), GPIO22-37 blocked) hung/crashed the whole
  // board: the library's internal serial handle was never set up. Found
  // via "haelt sich beim 3s-Bestaetigen auf" — the hold gesture itself
  // was fine, this call right after it wasn't. Fail safely instead.
  if (!g_initialized) {
    Serial.println("[MESH] send_emergency: Bridge nicht initialisiert, ueberspringe (kein Absturz mehr)");
    return false;
  }
  char buf[160];
  snprintf(buf, sizeof(buf), "LAGE:NEU;%s;offen;%s (Touch-Terminal, Typ: %s)", category, label, type);
  bool ok = mt_send_text(buf);
  Serial.printf("[MESH] send_emergency -> \"%s\" (%s)\n", buf, ok ? "OK" : "FEHLER");
  if (ok) ui_model_notify_tx();
  return ok;
}
