# SenseCAP Indicator (D1L) — Board-Notizen

Zweites unterstütztes Board neben der XIAO-ESP32-S3-Säule (siehe Haupt-README).
Touchscreen-Terminal mit Display, Datenbank und (angefangener) Meshtastic-Anbindung
auf **einem** Board statt der ESP32↔nRF52-Zwei-Board-Lösung.

Build/Flash: `pio run -e sensecap_indicator -t upload`

---

## 1. Status (Stand 2026-09-17)

| Teil | Status |
|---|---|
| Display (ST7701S 480x480 RGB) + Touch (FT6336U) | ✅ läuft stabil |
| Menü/Notfall-Flow (LVGL, `ui_model.cpp`) | ✅ läuft, inkl. Halte-Bestätigung, Sende-/Erfolg/Fehlschlag-Screens |
| Lagemeldungen in SQLite (`lage_db`, wiederverwendet von der XIAO-Säule) | ✅ läuft |
| Uhrzeit | ⚠️ nur Build-Zeitpunkt automatisch gesetzt, kein RTC/NTP — `settime YYYY-MM-DD HH:MM:SS` über Serial |
| Meshtastic-Anbindung | ⚠️ rohes LoRa (SX1262 direkt) sendet/läuft jetzt — aber noch **kein** Meshtastic-Protokoll (Verschlüsselung/Routing/Kanäle fehlen). Siehe [Issue #36](https://github.com/Pixeldieb/ESP32-S3-Meshtastic-Interface/issues/36) |
| Standby-Screen, Alarm-Blinken, Batterie/Solar/Netz-Symbol | ❌ noch nicht begonnen |
| Lokaler Betreiber / Onboarding | ⚠️ nur Datenstruktur (`station_config.h`), noch keine Eingabe-UI |

---

## 2. Hardware-Bring-up — was hart erkämpft war

Diese Einstellungen in `platformio.ini` (`env:sensecap_indicator`) sind **nicht optional**,
sondern durch Trial-and-Error an echter Hardware gefunden (siehe Git-Historie für die
volle Fehlersuche-Geschichte):

- `board_build.flash_mode = dio`, `board_build.arduino.memory_type = dio_opi` —
  jede QIO-Variante bootloopt (ROM meldet unabhängig von unserer Konfiguration `mode:DIO`,
  das PSRAM auf dieser Einheit ist Octal, nicht Quad).
- `board_build.partitions = default_8MB.csv` — Flash ist 8 MB
  (bestätigt via `esptool.py flash_id`), `default_16MB.csv` bootloopt.
- `platform = espressif32@5.4.0` (ESP-IDF 4.x) — `Arduino_GFX`s RGB-Panel-Treiber
  verträgt sich nicht mit ESP-IDF 5.x.
- Panel ist physisch **auf dem Kopf montiert** — 180°-Korrektur in
  `main.cpp` (`lvgl_disp_flush`) und `touch.cpp`, nicht im Treiber.

### Rendering-Glitches (das eigentliche Kernproblem)

Der Panel-Treiber dieser ESP-IDF-Version hat **keinen Doppelpuffer** — ein einzelner
PSRAM-Framebuffer wird kontinuierlich per DMA gescannt, während wir gleichzeitig
hineinschreiben. Behoben durch drei zusammenwirkende Maßnahmen (jede für sich war
nicht ausreichend):

1. **Cache-Writeback** nach jedem Schreiben (`Cache_WriteBack_Addr`) — PSRAM läuft über
   den CPU-Cache, die DMA liest direkt aus dem PSRAM und sah sonst veraltete Daten.
2. **Frame-Sync**: eigener `esp_lcd_new_rgb_panel`-Aufruf (nicht über `Arduino_GFX`,
   die das nicht exponiert) mit `on_frame_trans_done`-Callback — Schreibvorgänge warten
   auf den Beginn eines neuen Frames, statt an einem zufälligen Punkt mitten im Scan zu starten.
3. **PCLK gesenkt** (12→6 MHz): Ein voller Bildschirm-Kopiervorgang brauchte ~26-29ms,
   eine Bildperiode bei 12 MHz nur ~23ms — die Anzeige-Hardware hat uns also strukturell
   überholt, egal wie gut synchronisiert. Bei 6 MHz (~44ms Periode) reicht die Zeit.
   Kosten: niedrigere Bildwiederholrate (~22Hz), für ein Status-Menü unproblematisch.
4. Kopierrichtung im Flush-Loop an die physische Scan-Richtung angepasst (beide
   räumen in dieselbe Richtung, statt sich entgegenzulaufen).

**Falls nach dem nächsten LVGL-/Bibliotheks-Update wieder Glitches auftauchen:** zuerst
`disp_drv.full_refresh` prüfen (muss `0` sein, siehe Kommentar in `main.cpp`) und die
vier Punkte oben der Reihe nach neu verifizieren.

---

## 3. Architektur

- `main.cpp` — Display/Touch-Bring-up, LVGL-Glue, `setup()`/`loop()`.
- `display_profiles/sensecap_indicator_d1l.h` — alle Pin-/Timing-Konstanten für **dieses**
  Display. Neues Display = neue Profildatei mit denselben Makronamen + Include tauschen,
  sonst nichts anfassen.
- `io_expander.h/.cpp` — TCA9535-Treiber (I2C). Gated: LCD CS/RST, Touch-RST,
  LoRa NSS/RST/BUSY/DIO1.
- `touch.h/.cpp` — FT6336U (Single- und Dual-Touch-Lesen; Dual wird aktuell nirgends
  mehr gebraucht, das Panel hat ohnehin kein echtes Multitouch, siehe unten).
- `ui_model.h/.cpp` — das komplette Menü/Notfall-Flow (Hand-Nachbau von `ui/ui.yaml`,
  siehe `ui/README.md` Abschnitt 19 für das hardware-neutrale Bedienkonzept).
- `station_config.h/.cpp` — lokaler Betreiber/Stations-ID, Ansatzpunkt für ein
  künftiges Onboarding (noch keine Eingabe-UI).
- `wall_clock.h/.cpp` — echte Uhrzeit (kein RTC, wird beim Flashen aus der Build-Zeit
  gesetzt, sonst per `settime`).
- `meshtastic_bridge.h/.cpp` — externe-Node-Anbindung (Weg 2), **endgültig verworfen**
  (kein freier GPIO, RP2040 hat keine Hardware-Verbindung zum SX1262 — siehe unten).
- `lora_radio.h/.cpp` — Onboard-SX1262 direkt (Weg 1), **funktioniert** (SPI/Reset-Timing-
  Fix, siehe unten), aber weiterhin **nicht Meshtastic-protokoll-kompatibel** — nur rohes
  LoRa senden/empfangen.

---

## 4. Kein Multitouch

Die Halte-Bestätigung (`emergency_confirmation`) sollte ursprünglich zwei
Kontext-Tasten gleichzeitig gehalten verlangen (ui.yaml-Vorlage). Das Panel liefert
aber nachweislich keine zwei simultanen Touch-Punkte. Umgesetzt stattdessen als
**eine** Taste, 3 Sekunden halten (`confirm_btn_press_cb`, LVGL PRESSED/PRESSING/
RELEASED-Events, kein eigenes Touch-Polling mehr nötig).

---

## 5. Meshtastic-Anbindung — der offene Punkt

Siehe **[Issue #36](https://github.com/Pixeldieb/ESP32-S3-Meshtastic-Interface/issues/36)**
für den vollen Stand. Kurzfassung (Stand 2026-09-17, Update autonome Session):

- **Weg 1 (eingebautes SX1262 direkt, `lora_radio.cpp`): CHIP_NOT_FOUND gelöst.**
  Realer Schaltplan zum Board gefunden (öffentliches Referenzprojekt
  [ril3y/sensecap-indicator-d1l](https://github.com/ril3y/sensecap-indicator-d1l),
  `SENSECAP_INDICATOR_PINOUT_SCHEMATIC.md`) — bestätigt unsere IO-Expander-Pinbelegung
  (NSS=IO0, RST=IO1, BUSY=IO2, DIO1=IO3) exakt. Ursache für `CHIP_NOT_FOUND` war nicht
  die vermutete BUSY-über-I2C-Geschwindigkeit, sondern fehlende Settle-Zeit:
  `SX126x::reset()` pulst RST und hämmert danach *ohne jede Wartezeit* sofort
  `standby()` über SPI — auf echter Hardware antwortet der Chip da noch nicht
  zuverlässig. Per `RADIOLIB_DEBUG_SPI` bestätigt: alle `GET_STATUS`-Antworten während
  der 10x-Retry-Schleife kamen als `0x00 0x00` zurück (siehe `RADIOLIB_SX126X_REG_VERSION_STRING`-Dump).
  Fix in `lora_radio_wake_chip()`: eigener Reset + 20ms Settle-Zeit + ein rohes
  `GET_STATUS` als Lebenszeichen-Check, **bevor** RadioLib übernimmt. Danach: über
  drei Power-Cycles reproduzierbar `SX1262::begin() -> 0 (OK)` und
  `transmit() -> 0 (OK)`.
- **Weg 2 (externe Node über UART) endgültig verworfen, nicht nur wegen GPIO-Mangel:**
  Der Schaltplan zeigt außerdem, dass der RP2040-Co-Prozessor **keine** Hardware-Verbindung
  zum SX1262 hat — er ist ein reiner Sensor-Co-Prozessor (AHT20/SGP40/SCD41/SD-Karte/Buzzer)
  über ein fest verdrahtetes COBS-Binärprotokoll. Die früher angedachte "RP2040 als Relay"-
  Ausweichoption ist damit hinfällig (unabhängig davon, dass Weg 1 jetzt sowieso funktioniert).
- **Neuer, jetzt eigentlicher offener Punkt:** Rohes LoRa-Senden/Empfangen über den SX1262
  funktioniert, ist aber **kein** Meshtastic — kein kompatibles Paketformat, keine
  Verschlüsselung, kein Routing/Kanal-Handling. Echte Mesh-Kompatibilität bräuchte entweder
  (a) eine Nachbildung des Meshtastic-Protokolls auf Basis dieser rohen Funkverbindung
  (eigenständiger, nicht-trivialer Umfang), oder (b) eine bewusste Entscheidung, dass
  kayna-funkt-Geräte vorerst nur *untereinander* über ein eigenes, einfacheres Protokoll
  sprechen statt dem öffentlichen Meshtastic-Mesh beizutreten. Das ist eine strategische
  Scope-Frage, keine rein technische — sollte der Nutzer entscheiden, bevor daran
  weitergebaut wird.

---

## 6. Bekannte Platzhalter (bewusst, nicht vergessen)

- Info-Historie/Lageinformationen zeigen `lage_db`-Einträge inkl. 3 Beispieleinträgen,
  die beim ersten Boot einmalig geseedet werden (SPIFFS persistiert, kein erneutes
  Seeden bei jedem Boot).
- Status-Leiste: TX/RX-Punkte sind verdrahtet, blinken aber erst bei echtem Funkverkehr
  (`ui_model_notify_tx/rx`). ONLINE/OFFLINE hängt an `ui_model_set_connected`, aktuell
  fest `false` (keine Bridge aktiv). `testconnect on|off` über Serial überschreibt das
  nur zu Testzwecken — keine echte Verbindung.
- `do_trigger_emergency` speichert die Meldung immer in `lage_db` (Status wandert
  „wird übermittelt" → „übermittelt"/„fehlgeschlagen") — unabhängig davon, ob wirklich
  etwas gesendet wurde.
