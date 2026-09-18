# SenseCAP Indicator (D1L) — Board-Notizen

Zweites unterstütztes Board neben der XIAO-ESP32-S3-Säule (siehe Haupt-README).
Touchscreen-Terminal mit Display, Datenbank und (angefangener) Meshtastic-Anbindung
auf **einem** Board statt der ESP32↔nRF52-Zwei-Board-Lösung.

Build/Flash: `pio run -e sensecap_indicator -t upload`

---

## 1. Status (Stand 2026-09-18)

| Teil | Status |
|---|---|
| Display (ST7701S 480x480 RGB) + Touch (FT6336U) | ✅ läuft stabil |
| Menü/Notfall-Flow (LVGL, `ui_model.cpp`) | ✅ läuft, inkl. Halte-Bestätigung, Sende-/Erfolg/Fehlschlag-Screens |
| Lagemeldungen in SQLite (`lage_db`, wiederverwendet von der XIAO-Säule) | ✅ läuft |
| Uhrzeit | ⚠️ nur Build-Zeitpunkt automatisch gesetzt, kein RTC/NTP — `settime YYYY-MM-DD HH:MM:SS` über Serial |
| Meshtastic-Anbindung | ✅ **echtes Meshtastic-Protokoll** über den eingebauten SX1262 (Weg 1) — Paketheader, AES128-CTR-Verschlüsselung, Protobuf-Payload, Kanal-Hash, exakte Funkparameter, alles gegen den echten Firmware-Quellcode verifiziert (siehe Abschnitt 5). Sende- und Empfangspfad laufen; noch nicht gegen ein zweites echtes Meshtastic-Gerät getestet (keins zur Hand in dieser Session) |
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
- `lora_radio.h/.cpp` — Hardware-Bring-up des eingebauten SX1262 (Reset-Timing-Fix,
  RadioLibHal über den IO-Expander). Kennt nur den Chip, keine Meshtastic-Semantik.
- `meshtastic_proto.h/.cpp` — die eigentliche Meshtastic-Protokollschicht: Paketheader,
  AES128-CTR-Verschlüsselung, Protobuf-Encode/Decode, Kanal-Hash, Sende-/Empfangspfad.
  Siehe Abschnitt 5. (`meshtastic_bridge.h/.cpp`, die externe-Node-Anbindung "Weg 2",
  wurde entfernt — siehe Abschnitt 5, warum das endgültig keine Option auf diesem Board ist.)

---

## 4. Kein Multitouch

Die Halte-Bestätigung (`emergency_confirmation`) sollte ursprünglich zwei
Kontext-Tasten gleichzeitig gehalten verlangen (ui.yaml-Vorlage). Das Panel liefert
aber nachweislich keine zwei simultanen Touch-Punkte. Umgesetzt stattdessen als
**eine** Taste, 3 Sekunden halten (`confirm_btn_press_cb`, LVGL PRESSED/PRESSING/
RELEASED-Events, kein eigenes Touch-Polling mehr nötig).

---

## 5. Meshtastic-Anbindung

Siehe **[Issue #36](https://github.com/Pixeldieb/ESP32-S3-Meshtastic-Interface/issues/36)**
für den vollen Verlauf. Kurzfassung, Stand 2026-09-18 (autonome Nachtsession):

### 5.1 CHIP_NOT_FOUND gelöst (Weg 1, `lora_radio.cpp`)

Realer Schaltplan zum Board gefunden (öffentliches Referenzprojekt
[ril3y/sensecap-indicator-d1l](https://github.com/ril3y/sensecap-indicator-d1l),
`SENSECAP_INDICATOR_PINOUT_SCHEMATIC.md`) — bestätigt unsere IO-Expander-Pinbelegung
(NSS=IO0, RST=IO1, BUSY=IO2, DIO1=IO3) exakt. Ursache für `CHIP_NOT_FOUND` war nicht
die vermutete BUSY-über-I2C-Geschwindigkeit, sondern fehlende Settle-Zeit:
`SX126x::reset()` pulst RST und hämmert danach *ohne jede Wartezeit* sofort `standby()`
über SPI. Fix in `lora_radio_wake_chip()`: eigener Reset + 20ms Settle-Zeit + ein rohes
`GET_STATUS` als Lebenszeichen-Check, **bevor** RadioLib übernimmt.

Der Schaltplan zeigt außerdem: der RP2040-Co-Prozessor hat **keine** Hardware-Verbindung
zum SX1262 (reiner Sensor-Co-Prozessor über ein festes COBS-Binärprotokoll) — die früher
angedachte "RP2040 als Relay"-Ausweichoption für Weg 2 ist damit hinfällig, und Weg 2
(externe Node über UART) bleibt aus GPIO-Mangel verworfen. `meshtastic_bridge.h/.cpp`
wurde entfernt.

### 5.2 Echtes Meshtastic-Protokoll (nicht nur rohes LoRa)

`meshtastic_proto.h/.cpp` implementiert das tatsächliche Meshtastic-Wire-Format auf dem
öffentlichen Default-Primärkanal ("LongFast", Standard-PSK) — kein eigenes/inkompatibles
Format. Jede Konstante ist direkt aus `meshtastic/firmware`s eigenem Quellcode gelesen
(nicht aus dem Gedächtnis rekonstruiert):

| Was | Wert | Quelle |
|---|---|---|
| Paketheader (16 Byte: to/from/id/flags/channel/next_hop/relay_node) | exakter Byte-Layout | `RadioInterface.h` (`PacketHeader`) |
| Verschlüsselung | AES128-CTR, Nonce = 8B PacketID (LE) + 4B NodeNum (LE) + 4B Zähler | `CryptoEngine.cpp/.h` |
| Default-Kanal-PSK | `d4 f1 bb 3a 20 29 07 59 f0 bc ff ab cf 4e 69 01` (öffentlich, kein Geheimnis) | `Channels.h` (`defaultpsk`) |
| Kanal-Hash | `xorHash("LongFast") ^ xorHash(psk)` | `Channels.cpp` (`generateHash`) |
| Payload | Protobuf-`Data`-Message (Portnum + Bytes) | `mesh.pb.h`, vendored in `lib/meshtastic_proto/` |
| Frequenz (Region EU_868 + Preset LONG_FAST) | **869.525 MHz**, genau ein Kanal-Slot (kein Hopping — `numChannels = floor(0.25/0.25) = 1`) | `RadioInterface.cpp` (`applyModemConfig`, `regions[]`) |
| BW/SF/CR/Sync/Präambel | 250kHz / SF11 / 4:5 / `0x2b` / 16 Symbole | `MeshRadio.h`, `RadioLibInterface.h`, `RadioInterface.h` |

Protobuf: nicht händisch kodiert, sondern nanopb 0.4.9.1 + Meshtastics eigene generierte
`.pb.h/.cpp` vendored in `lib/meshtastic_proto/` (siehe dessen README für Provenienz) —
das eliminiert praktisch jedes Risiko eines Feldnummern-/Wire-Type-Fehlers.

**Verifiziert, nicht nur behauptet** (Ehrlichkeits-Grundsatz dieses Projekts): das eigene
Gerät hat sein eigenes gesendetes Paket per HF-Selbstkopplung empfangen und korrekt
entschlüsselt/dekodiert (voller Roundtrip: Encode → Verschlüsseln → Senden → Empfangen →
Entschlüsseln → Decode). Zusätzlich wurden Klartext- und Chiffretext-Bytes eines echten
gesendeten Pakets abgegriffen und **unabhängig** in Python (pycryptodome, komplett andere
Implementierung als das on-device mbedtls) nachgerechnet — Ergebnis war byte-identisch.
Das bestätigt AES-CTR-Nonce-Konstruktion und Protobuf-Encoding gegen eine zweite,
unabhängige Implementierung. **Nicht verifiziert:** Empfang/Versand gegen ein zweites
echtes Meshtastic-Gerät (App oder Node) — keins in dieser Session zur Hand. Das ist der
eigentliche Test für morgen früh.

### 5.3 Was zum Testen bereitsteht

- Serial-Kommando `mesh send <text>` — sendet eine echte Meshtastic-Textnachricht auf
  dem Default-Kanal. Mit der offiziellen Meshtastic-App (oder einem echten Node) auf
  EU868 + LongFast-Preset (Werkseinstellung) sollte das ankommen.
- Der Notfall-Bestätigungs-Flow (`ui_model.cpp` → `meshtastic_send_emergency()`) sendet
  jetzt echte Meshtastic-Textnachrichten im bekannten `LAGE:...`-Format statt der alten
  UART-Bridge.
- Empfang läuft mit: eingehende Textnachrichten (auch von echten Fremdgeräten auf
  demselben Kanal) werden dekodiert, geloggt, und `LAGE:`-formatierte Nachrichten in
  `lage_db` übernommen (`ui_model_notify_rx()` für die Statusleiste).
- ONLINE/OFFLINE in der Statusleiste zeigt jetzt einen echten Zustand: `true` sobald
  `lora_radio_begin()` **und** `meshtastic_proto_begin()` beim Boot erfolgreich waren.

### 5.4 Bewusst nicht gemacht

- Kein Routing/Rebroadcast (Store-and-Forward über mehrere Hops) — wir senden/empfangen
  nur direkt, wie ein einfacher Leaf-Node.
  Kein DIO1-Hardware-Interrupt (der Pin hängt am IO-Expander, nicht an einem echten
  ESP32-GPIO) — `meshtastic_proto_loop()` pollt stattdessen `getIrqStatus()` per SPI
  jede `loop()`-Iteration, unkritisch für die Latenz dieses Projekts.
- Kein PKI/Direktnachrichten (Curve25519), keine Zusatzkanäle — nur der öffentliche
  Default-Kanal.
- Node-Nummer wird aus der ESP32-MAC abgeleitet, nicht mit Meshtastics eigenem Algorithmus
  nachgebildet — für die Protokoll-Kompatibilität irrelevant (jede stabile, von 0 und
  Broadcast verschiedene 32-Bit-Zahl funktioniert).

---

## 6. Bekannte Platzhalter (bewusst, nicht vergessen)

- Info-Historie/Lageinformationen zeigen `lage_db`-Einträge inkl. 3 Beispieleinträgen,
  die beim ersten Boot einmalig geseedet werden (SPIFFS persistiert, kein erneutes
  Seeden bei jedem Boot).
- Status-Leiste: TX/RX-Punkte sind verdrahtet und blinken bei echtem Funkverkehr
  (`ui_model_notify_tx/rx`). ONLINE/OFFLINE hängt an `ui_model_set_connected`, gesetzt
  in `main.cpp` sobald Radio+Protokoll beim Boot erfolgreich starten (siehe Abschnitt 5).
  `testconnect on|off` über Serial kann das zu Testzwecken übersteuern.
- `do_trigger_emergency` speichert die Meldung immer in `lage_db` (Status wandert
  „wird übermittelt" → „übermittelt"/„fehlgeschlagen") — unabhängig davon, ob wirklich
  etwas gesendet wurde.
