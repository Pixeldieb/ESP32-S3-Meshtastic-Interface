<div align="center">

# kayna-funkt Notmeldeterminal (vibe coded prototype)

![Platform](https://img.shields.io/badge/platform-ESP32--S3-10537E?style=flat-square)
![Framework](https://img.shields.io/badge/framework-Arduino%20%2F%20PlatformIO-04A098?style=flat-square)
![Node](https://img.shields.io/badge/mesh-Meshtastic-62B22E?style=flat-square)
![Status](https://img.shields.io/badge/status-prototype%20%E2%80%94%20kayna--funkt-lightgrey?style=flat-square)
![AI Slopmaker](https://img.shields.io/badge/Anthropic%20Claude%20Sonnet%205%20High)

Ein Meshtastic-basiertes Notmeldeterminal für abgesetzte Standorte ohne
Mobilfunk-/Internetanbindung — Teil des Notmeldestellen-Projekts **kayna-funkt**.
Zwei Hardware-Wege werden verfolgt: ein eigenständiges Touchscreen-Terminal
(**SenseCAP Indicator**, eingebautes LoRa-Funkmodul, aktueller Entwicklungsfokus)
und ein serieller Prototyp (**Seeed XIAO ESP32-S3** + externe Meshtastic-Node
über UART, ursprünglicher Ausgangspunkt des Projekts). Frühere Titel dieses
Repos ("ESP32-S3 Interface for Meshtastic") beschrieben nur noch den zweiten,
mittlerweile nicht mehr priorisierten Weg — siehe [Wiki](https://github.com/Pixeldieb/kayna-funkt-notmeldeterminal/wiki) für den aktuellen Gesamtüberblick.

</div>

---

## 📋 Inhalt

- [Was macht dieses Projekt?](#-was-macht-dieses-projekt)
- [Hardware](#-hardware)
- [Verkabelung](#-verkabelung)
- [Software-Setup](#-software-setup)
- [Meshtastic-Node per CLI vorbereiten](#-meshtastic-node-per-cli-vorbereiten)
- [Wie der Code funktioniert](#-wie-der-code-funktioniert)
- [🚦 Zustandsmodell der Säule](#-zustandsmodell-der-säule)
- [🚨 Lagemeldungen (kayna-funkt)](#-lagemeldungen-kayna-funkt)
- [Testen](#-testen-ob-alles-funktioniert)
- [Troubleshooting](#-troubleshooting)
- [Weiterführende Links](#-weiterführende-links)
- [📺 SenseCAP Indicator (zweites Board)](#-sensecap-indicator-zweites-board)
- [Changelog](#-changelog)
- [Lizenz](#-lizenz)

---

## 🧭 Was macht dieses Projekt?

Zwei kleine Microcontroller-Boards, seriell (UART) verbunden:

| Board | Rolle |
|---|---|
| **Seeed XIAO ESP32-S3** | Führt den Code aus diesem Repo aus, speichert Lagemeldungen in einer lokalen Datenbank |
| **Seeed XIAO nRF52** (Stock Meshtastic-Firmware) | Übernimmt das Funken (LoRa) ins Mesh-Netzwerk |

Der ESP32-S3 hat **kein eigenes LoRa-Funkmodul** — er nutzt die Meshtastic-Node als "Funk-Anhängsel" und spricht mit ihr über eine serielle Verbindung.

**Ziel des Prototyps:** Eingehende, speziell markierte Nachrichten ("Lagemeldungen") sollen nicht nur als lose Chat-Nachricht im Mesh verpuffen, sondern strukturiert und mit Änderungshistorie auf dem Gerät gespeichert werden — als echtes Backup zur reinen Meshtastic-Nachrichtenliste.

---

## 🔧 Hardware

| Teil | Wofür | Mehr Infos |
|---|---|---|
| Seeed XIAO ESP32-S3 | Führt den Code aus | [Seeed Wiki: Getting Started](https://wiki.seeedstudio.com/xiao_esp32s3_getting_started/) · [Pin-Belegung](https://wiki.seeedstudio.com/xiao_esp32s3_pin_multiplexing/) |
| Seeed XIAO nRF52 + Meshtastic-Firmware | Funkt ins Mesh-Netzwerk | [Meshtastic-Firmware flashen](https://flasher.meshtastic.org/) |
| Breadboard | Verbindung beider Boards | – |
| 2× USB-C-Kabel (Datenkabel!) | Strom + Programmieren | – |
| Jumper-Kabel | Für die Verkabelung | – |

> ⚠️ **Achtung bei den USB-Kabeln:** Manche USB-C-Kabel sind reine Ladekabel ohne Datenleitungen. Häufigste Ursache für Upload-Fehler.

---

## 🔌 Verkabelung

TX/RX **gekreuzt**, GND gemeinsam:

| XIAO ESP32-S3 | Meshtastic-Node |
|---|---|
| D6 (TX, GPIO43) | → RX |
| D7 (RX, GPIO44) | ← TX |
| GND | ↔ GND (Pflicht, auch bei getrennter USB-Stromversorgung) |

---

## 💻 Software-Setup

### 1. Entwicklungsumgebung

- [VS Codium](https://vscodium.com/) + [PlatformIO-Erweiterung](https://platformio.org/install/ide?install=vscode)

### 2. platformio.ini

```ini
[env:seeed_xiao_esp32s3]
platform = espressif32
board = seeed_xiao_esp32s3
framework = arduino
monitor_speed = 115200
lib_deps =
    https://github.com/meshtastic/Meshtastic-arduino.git
    siara-cc/Sqlite3Esp32
```

### 3. Firmware flashen

```bash
pio run --target upload
```

---

## 📡 Meshtastic-Node per CLI vorbereiten

Einmalig **per USB direkt am Rechner** (nicht über D6/D7):

```bash
pip3 install --upgrade meshtastic

meshtastic --set lora.region EU_868
meshtastic --set lora.tx_enabled true

meshtastic --set serial.enabled true
meshtastic --set serial.mode PROTO
meshtastic --set serial.rxd 7
meshtastic --set serial.txd 6
meshtastic --set serial.baud BAUD_115200

meshtastic --reboot
```

📖 Details: [meshtastic.org/docs/software/python/cli](https://meshtastic.org/docs/software/python/cli/)

---

## 🧩 Wie der Code funktioniert

| Funktion | Aufgabe |
|---|---|
| `mt_serial_init(...)` | Baut beim Start die Verbindung zur Node auf |
| `mt_loop(millis())` | Muss **jeden Durchlauf** aufgerufen werden — hält Verbindung + eingehende Nachrichten am Laufen |
| `mt_send_text(...)` | Sendet alle 5 Minuten eine Test-Nachricht als Broadcast |
| `set_text_message_callback(...)` | Registriert `onTextMessage()`, wird bei jeder eingehenden Textnachricht aufgerufen |

**LED-Status** (eingebaute LED, GPIO21):

| Muster | Bedeutung |
|---|---|
| Doppel-Blitz + Pause | Verbindungsaufbau (Handshake) läuft |
| 5× schnelles Blinken (einmalig) | Verbindung erfolgreich hergestellt |
| Langsames Blinken (500ms) | Normalbetrieb |
| 6× schnelles Blinken | Test-Nachricht wird gesendet |
| 3× schnelles Blinken | Lagemeldung wurde gespeichert/aktualisiert |

---

## 🚦 Zustandsmodell der Säule

Die Säule kennt fünf Betriebszustände. Umschalten per Kurz-Code als Mesh-Broadcast (Textnachricht):

| Code | Zustand | Bedeutung |
|---|---|---|
| `LGE` | `AKTIV` | Lageöffnung — Notfallbetrieb |
| `NOR` | `STANDBY` | Normalbetrieb (Startzustand nach Boot) |
| `WTG` | `WARTUNG` | Wartungsmodus |
| `SAB` | `SABOTAGE` | Sabotage erkannt (aktuell manueller Test-Trigger) |
| `SAUS` | `STROMAUSFALL` | Stromausfall-Betrieb (aktuell manueller Test-Trigger, spätere Ausbaustufe: automatische Erkennung) |

Codes bewusst kurz und eindeutig gehalten — keine Alltagswörter wie "aus", um versehentliches Auslösen und Tippfehler zu vermeiden, und schnell tippbar auch unter Stress oder auf kleiner Tastatur.

Aktuellen Zustand abfragen: Serial-Befehl `status`.

> 📌 **Aktueller Stand:** Nur der reine Zustandswechsel ist implementiert, noch ohne Prüfung, ob der Absender berechtigt ist (siehe Troubleshooting/Security-Hinweis unten). Automatische Erkennung von Stromausfall/Sabotage über Hardware sowie die Leitstellen-Anbindung folgen in späteren Schritten.

> 🔐 **Sicherheitshinweis:** Aktuell kann jede Node im selben Mesh-Kanal per Kurz-Code den Zustand der Säule ändern — es gibt noch keine Absender-Prüfung. Für den Feldtest tragbar, vor einem echten Einsatz muss das über eine Absender-Allowlist abgesichert werden (siehe [Issue #1](https://github.com/Pixeldieb/kayna-funkt-notmeldeterminal/issues/1) im Hauptrepo).

---

## 🚨 Lagemeldungen (kayna-funkt)

Eingehende Nachrichten mit dem Prefix `LAGE:` werden erkannt, geparst und in einer lokalen **SQLite-Datenbank** (`/spiffs/lage.db`) gespeichert — alle anderen Mesh-Nachrichten werden ignoriert.

### Nachrichtenformat

```
LAGE:<ID|NEU>;<Kategorie>;<Status>;<Text>
```

**Neue Lagemeldung anlegen:**
```
LAGE:NEU;Brand;offen;Kellerbrand Mehrfamilienhaus Hauptstraße 12
```

**Bestehende Lagemeldung aktualisieren** (ID aus vorheriger Anlage, z.B. `1`):
```
LAGE:1;Brand;in Bearbeitung;Feuerwehr löscht, Nachbargebäude evakuiert
```

Trennzeichen ist bewusst **Semikolon** (`;`) statt Pipe — auf jeder Tastatur ohne Umschalt-Kombination erreichbar, wichtig im Feldeinsatz.

### Datenmodell

| Tabelle | Zweck |
|---|---|
| `lagemeldungen` | Aktueller Stand jeder Lagemeldung (Kategorie, Status, Text, Absender, Zeitstempel) |
| `lage_historie` | Jede Änderung wird **vor** dem Überschreiben protokolliert (alter/neuer Text, alter/neuer Status, Zeitpunkt) — volle Nachvollziehbarkeit |

IDs laufen über SQLites eingebaute `rowid`, keine expliziten `PRIMARY KEY`/`UNIQUE`-Constraints (siehe [Troubleshooting](#-troubleshooting) — Grund dafür ist ein bekannter Bibliotheks-Bug).

### Serial-CLI zum Prüfen

Im seriellen Monitor eintippen:

| Befehl | Zeigt |
|---|---|
| `liste` | Alle aktuellen Lagemeldungen |
| `liste kategorie <X>` | Gefiltert nach Kategorie |
| `liste status <X>` | Gefiltert nach Status |
| `detail <ID>` | Eine Lagemeldung inkl. kompletter Änderungshistorie |
| `status` | Aktueller Zustand der Säule (siehe [Zustandsmodell](#-zustandsmodell-der-säule)) |
| `help` | Befehlsübersicht |

> 📌 **Aktueller Stand:** Anzeige nur über den seriellen Monitor (CLI). Ein grafisches Interface (HTML, mit zentraler Datenbank-Anbindung und Anzeige auf verschiedenen Displays, u.a. ePaper) ist für eine spätere Ausbaustufe geplant — die Datenbank auf dem ESP32 dient dann primär als lokaler Offline-Puffer/Backup.
>
> ⏱️ **Bekannte Einschränkung:** Zeitstempel basieren aktuell auf `millis()` (Geräte-Uptime seit letztem Reboot), keine echte Wanduhrzeit. NTP-Sync via WLAN ist vorbereitet, aber noch nicht eingebaut.

---

## ✅ Testen, ob alles funktioniert

1. Seriellen Monitor öffnen: `pio run --target monitor`
2. `>>> VERBUNDEN mit der Node` sollte erscheinen
3. Von einem zweiten Gerät im Mesh eine Lagemeldung senden (siehe oben)
4. Im Monitor sollte erscheinen: `>>> Neue Lagemeldung angelegt, ID X`
5. `liste` eintippen → sollte die neue Meldung zeigen

### Automatisierter Funktionstest

[`scripts/test_mesh_functions.sh`](./scripts/test_mesh_functions.sh) sendet nacheinander alle aktuell unterstützten Testnachrichten (Zustandswechsel-Codes + Lagemeldungen) über einen zweiten, per USB angeschlossenen Meshtastic-Node.

**Voraussetzungen:**
- `meshtastic`-CLI installiert: `pip3 install --upgrade meshtastic`
- Zweiter Meshtastic-Node per USB angeschlossen, **Region gesetzt** (`meshtastic --port <port> --set lora.region EU_868`) und auf demselben Kanal wie der Brain-Node

**Ausführen:**

```bash
./scripts/test_mesh_functions.sh --port /dev/cu.usbmodem2101
```

Seriellen Monitor des Brain-Boards währenddessen offen halten und nach jeder gesendeten Nachricht mit `status`, `liste` bzw. `detail <ID>` gegenprüfen. Optional Wartezeit zwischen den Nachrichten anpassen: `--delay <Sekunden>` (Default: 3).

---

## 🩺 Troubleshooting

| Problem | Ursache | Fix |
|---|---|---|
| Upload schlägt fehl (`serial noise`, Timeout) | Anderes Terminal/Monitor blockiert den Port | Alle anderen Terminals schließen |
| Rote LED leuchtet dauerhaft bei USB | Normal — eingebaute Charge-LED | Kein Fehler |
| `SQL-Fehler: disk I/O error` beim Start | **Bekannter Bug** der `Sqlite3Esp32`-Bibliothek: `PRIMARY KEY`/`UNIQUE`-Constraints lösen auf SPIFFS zuverlässig I/O-Fehler aus ([Issue #18](https://github.com/siara-cc/esp32_arduino_sqlite3_lib/issues/18)) | Behoben: Schema nutzt keine expliziten Constraints mehr, IDs laufen über SQLites eingebaute `rowid` |
| Monitor zeigt nichts/Datenmüll | `monitor_speed` in `platformio.ini` passt nicht zu `Serial.begin()` im Code | `monitor_speed = 115200` setzen |
| Node sendet laut CLI erfolgreich, App zeigt nichts | App noch per USB verbunden (Port-Konflikt) oder Bluetooth-Kopplung verloren | Node nur per Strom + D6/D7 betreiben, App per **Bluetooth** verbinden |

---

## 📺 SenseCAP Indicator (zweites Board)

Zweites unterstütztes Board: ein Seeed SenseCAP Indicator (D1L) mit 480×480-
Touchscreen, das Menü/Notfall-Flow/Datenbank auf **einem** Board zeigt statt der
ESP32↔nRF52-Zwei-Board-Lösung oben.

Build/Flash: `pio run -e sensecap_indicator -t upload`

Ausführliche Doku (Hardware-Bring-up-Story, bekannte Gotchas, Architektur,
aktueller Stand der Meshtastic-Anbindung): **[src/sensecap/README.md](src/sensecap/README.md)**.

Kurzstand: Display/Touch/Menü/Datenbank laufen stabil. Meshtastic-Anbindung läuft
über den eingebauten SX1262 mit echtem Meshtastic-Protokoll (Paketformat,
Verschlüsselung, Kanal-Hash — kein rohes/inkompatibles Signal) und ist gegen ein
reales Meshtastic-Gerät verifiziert: Broadcast bidirektional, Direktnachricht mit
echter Zustellbestätigung (ROUTING_APP-ACK) an eine konfigurierbare Leitstelle.
Details im [Wiki](https://github.com/Pixeldieb/kayna-funkt-notmeldeterminal/wiki/SenseCAP-Meshtastic)
und in [Issue #36](https://github.com/Pixeldieb/kayna-funkt-notmeldeterminal/issues/36).

---

## 🔗 Weiterführende Links

- [Projekt-Wiki](https://github.com/Pixeldieb/kayna-funkt-notmeldeterminal/wiki) — Status-Dashboard über beide Boards und alle Themenbereiche
- [Seeed XIAO ESP32-S3 – Getting Started](https://wiki.seeedstudio.com/xiao_esp32s3_getting_started/)
- [Meshtastic – Offizielle Dokumentation](https://meshtastic.org/docs/)
- [Meshtastic – Serial Module Konfiguration](https://meshtastic.org/docs/configuration/module/serial/)
- [Meshtastic-Arduino Library (GitHub)](https://github.com/meshtastic/Meshtastic-arduino)
- [Sqlite3Esp32 Library (GitHub)](https://github.com/siara-cc/esp32_arduino_sqlite3_lib)
- [PlatformIO Dokumentation](https://docs.platformio.org/)

---

## 📝 Changelog

### 2026-09-18

- **SenseCAP Indicator: echtes Meshtastic-Protokoll** über den eingebauten SX1262
  (nicht mehr nur rohes LoRa) — Paketheader, AES128-CTR-Verschlüsselung, Protobuf-
  Payload und Kanal-Hash exakt nach `meshtastic/firmware`s eigenem Quellcode
  nachgebaut (Frequenz/BW/SF/CR/Sync/Präambel ebenso). Protobuf-Definitionen nicht
  handkodiert, sondern nanopb + Meshtastics eigene generierte Header vendored
  (`lib/meshtastic_proto/`). Details: [src/sensecap/README.md](src/sensecap/README.md)
  Abschnitt 5, [Issue #36](https://github.com/Pixeldieb/kayna-funkt-notmeldeterminal/issues/36).
- Die alte externe-Node-Bridge (`meshtastic_bridge.h/.cpp`, Weg 2) für dieses Board
  entfernt — endgültig nicht machbar (kein freier GPIO, RP2040-Co-Prozessor hat keine
  Hardware-Verbindung zum SX1262, siehe Board-README).
- **Live gegen ein echtes Meshtastic-Gerät verifiziert** (Folgesession, selber Tag):
  Broadcast bidirektional bestätigt, danach zwei reale Blocker gefunden und behoben —
  moderne Firmware lehnt nicht-PKI-Direktnachrichten auf `TEXT_MESSAGE_APP` ab
  ("legacy DM", Fix: eigener `PRIVATE_APP`-Portnum), und Broadcasts werden nie
  bestätigt (Fix: eigener privater Kanal + Direktnachricht mit echtem
  `ROUTING_APP`-ACK an eine konfigurierbare Leitstelle). NodeInfo-Austausch ergänzt.
  Nebenbei einen unabhängigen, bis dahin unbemerkten Bug gefunden: `lageDbBegin()`
  fehlte auf diesem Board komplett, die Notmeldungshistorie lief seit Board-Einführung
  ins Leere. Details im [Wiki](https://github.com/Pixeldieb/kayna-funkt-notmeldeterminal/wiki/SenseCAP-Meshtastic).
- **Sicherheitslücken aus der Codebase-Analyse geschlossen** ([#1](https://github.com/Pixeldieb/kayna-funkt-notmeldeterminal/issues/1),
  [#3](https://github.com/Pixeldieb/kayna-funkt-notmeldeterminal/issues/3)): eingehende
  Lagemeldungen (und auf dem XIAO-Board die Zustands-Kurzcodes) wurden von jedem
  Absender ungeprüft übernommen. Neues gemeinsames Modul `src/common/mesh_security.h`
  mit Absender-Allowlist (sicherer Default: leer = alles verwerfen, nicht alles
  erlauben) und Ratenbegrenzung, in beide Boards eingebaut.
- **Heartbeat-Telemetrie** ([#13](https://github.com/Pixeldieb/kayna-funkt-notmeldeterminal/issues/13)):
  SenseCAP-Board sendet periodisch einen Status-Broadcast, damit eine Leitstelle
  eine ausgefallene Station am ausbleibenden Lebenszeichen erkennt — ehrlich ohne
  Akku-/Sabotage-Werte, da dafür noch keine Sensorik existiert.

### 2026-09-17

- **Zweites Board: Seeed SenseCAP Indicator (D1L)** — eigenständiges Touchscreen-
  Terminal (480×480, ST7701S/FT6336U), `env:sensecap_indicator` in `platformio.ini`.
  Menü/Notfall-Flow (Auswahl → Bestätigung mit Halte-Geste → Senden → Erfolg/
  Fehlschlag), Lagemeldungen in SQLite (wiederverwendet von der XIAO-Säule),
  Statusleiste, Notmeldungshistorie. Details: [src/sensecap/README.md](src/sensecap/README.md).
- Umfangreiches Hardware-Bring-up nötig: Bootloop-Ursachen (PSRAM-/Flash-Modus,
  Partitionstabelle), auf dem Kopf montiertes Panel, und Rendering-Glitches durch
  fehlenden Doppelpuffer (behoben über Cache-Writeback + Frame-Sync-Callback +
  reduzierten Pixeltakt) — siehe Board-README für die volle Fehlersuche-Geschichte.
- Meshtastic-Anbindung für das neue Board: eingebautes SX1262 sendet/empfängt jetzt
  zuverlässig rohes LoRa (Reset-Settle-Timing-Fix, nicht das vermutete BUSY-Timing).
  Externe Node über UART bleibt aus GPIO-Mangel verworfen. Echte Meshtastic-Protokoll-
  Kompatibilität (Verschlüsselung/Routing/Kanäle) ist der jetzt eigentliche offene
  Punkt — strategische Scope-Frage, dokumentiert in [Issue #36](https://github.com/Pixeldieb/kayna-funkt-notmeldeterminal/issues/36).
- `src/` neu strukturiert für mehrere Boards: `src/xiao/`, `src/common/`
  (gemeinsam genutztes `lage_db`), `src/sensecap/`.

### 2026-09-16

- **Zustandsmodell der Säule ergänzt** (STANDBY/AKTIV/STROMAUSFALL/WARTUNG/SABOTAGE), abfragbar per neuem Serial-Befehl `status`
- Zustandswechsel per Kurz-Code als Mesh-Broadcast (`LGE`, `NOR`, `WTG`, `SAB`, `SAUS`) — bewusst kurz und ohne Alltagswörter, um Tippfehler/versehentliches Auslösen zu vermeiden
- Feature-Roadmap strukturiert: Aufteilung in drei Repos (Mesh-Brain hier, [notfallbox-update-station](https://github.com/Pixeldieb/notfallbox-update-station) für Captive Portal/OTA, privates `notmeldestelle-leitstelle-spec` für die Leitstellen-Schnittstelle), Milestones „MVP Feldtest Kayna/Zeitz" und „Post-Pilot"
- `upload_speed` in `platformio.ini` auf 115200 gesenkt (460800 war beim Flashen über USB unzuverlässig)

### 2026-09-15

- Grundgerüst: ESP32-S3 ↔ Meshtastic-Node über D6/D7 (UART, gekreuzt), Verbindungsaufbau mit vollständigem Handshake-Wait
- LED-Statusanzeige (Warte-/Erfolgs-/Sende-/Heartbeat-Muster) über die eingebaute LED
- Test-Sendung im festen Intervall (zuletzt: alle 5 Minuten)
- Meshtastic-Node per CLI vollständig eingerichtet (Region, Serial-Modul, Owner, Kanal-Check)
- **Lagemeldungen-Feature ergänzt:** eingehende `LAGE:`-Nachrichten werden geparst und gespeichert
  - Nachrichtenformat zunächst mit Pipe (`|`), auf Anwenderwunsch auf Semikolon (`;`) umgestellt
  - Speicherung zunächst als JSON-Datei (LittleFS + ArduinoJson) prototypisch umgesetzt
  - Auf Wunsch durch echte relationale Datenbank ersetzt: SQLite (`Sqlite3Esp32`) mit zwei Tabellen (`lagemeldungen`, `lage_historie`) für Filterung, Kategorien und vollständige Änderungshistorie
  - Serial-CLI-Befehle (`liste`, `liste kategorie`, `liste status`, `detail`) zum Prüfen ohne zusätzliches Interface
  - Bug behoben: `disk I/O error` durch `PRIMARY KEY`/`UNIQUE`-Constraints auf SPIFFS (bekannter Library-Bug) — Schema auf implizite `rowid` umgestellt
- Wissensdatenbank-Artikel in Odoo Knowledge angelegt und laufend um Node-Vorbereitung (CLI-Checks, Einstellungen, Firmware-Update-Weg für XIAO nRF52840) erweitert

---

## 📄 Lizenz

Siehe [LICENSE](./LICENSE).
