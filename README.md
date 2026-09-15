ESP32-S3 Interface for Meshtastic

Ein Lernprojekt: Ein Seeed XIAO ESP32-S3 sendet automatisch alle 20 Sekunden eine Textnachricht über eine angeschlossene Meshtastic-Node (Seeed XIAO nRF52) ins Mesh-Netzwerk — inklusive LED-Statusanzeige, damit man sieht, dass alles läuft.

Was macht dieses Projekt eigentlich?

Zwei kleine Microcontroller-Boards werden über eine Kabelverbindung (UART/seriell) miteinander verbunden:

Seeed XIAO ESP32-S3 — läuft der eigentliche Code, den du in diesem Repo findest. Er "spricht" mit der zweiten Platine und gibt ihr den Befehl "sende jetzt eine Nachricht".
Seeed XIAO nRF52 (Meshtastic-Node) — läuft die fertige Meshtastic-Firmware und übernimmt das eigentliche Funken (LoRa) ins Mesh-Netzwerk.

Der ESP32-S3 selbst hat kein LoRa-Funkmodul — er nutzt die Meshtastic-Node quasi als "Funk-Anhängsel", mit dem er über eine einfache Kabelverbindung spricht.

Was du brauchst (Hardware)
Teil	Wofür
Seeed XIAO ESP32-S3	Führt den Code aus diesem Repo aus
Seeed XIAO nRF52 (mit Meshtastic-Firmware)	Übernimmt das Funken ins Mesh
Breadboard	Zum Verbinden beider Boards
2× USB-C-Kabel (Datenkabel, keine reinen Ladekabel!)	Strom + Programmieren
Jumper-Kabel	Für die Verkabelung
Verkabelung

Die beiden Boards werden über UART (seriell) gekreuzt verbunden:

XIAO ESP32-S3	Meshtastic-Node (XIAO nRF52)
D6 (TX, GPIO43)	→ RX der Node
D7 (RX, GPIO44)	← TX der Node
GND	↔ GND (gemeinsame Masse — Pflicht!)

Wichtig: TX und RX müssen gekreuzt werden (TX → RX, RX → TX), nicht 1:1 verbunden. Beide GND-Pins müssen über eine gemeinsame Schiene auf dem Breadboard verbunden sein, auch wenn beide Boards separat über USB mit Strom versorgt werden — sonst haben die beiden Boards kein gemeinsames Spannungs-Referenzniveau und die Kommunikation funktioniert nicht.

Software-Setup
1. Entwicklungsumgebung
VS Codium (oder VS Code)
PlatformIO-Erweiterung darin installieren
2. Projekt öffnen

Dieses Repo klonen oder herunterladen, dann in VS Codium über PlatformIO → Projects → Add Existing öffnen.

Die Bibliothek Meshtastic-arduino wird automatisch über platformio.ini (lib_deps) beim ersten Build heruntergeladen — kein manueller Schritt nötig.

3. Firmware auf den ESP32-S3 flashen
ESP32-S3 per USB-C anschließen
In der PlatformIO-Seitenleiste (Ameisen-Symbol links) unter PROJECT TASKS → seeed_xiao_esp32s3 → General → Upload klicken
Alternativ im Terminal: pio run --target upload

Häufige Stolpersteine dabei:

"Could not configure port" oder "serial noise": Meist, weil noch ein anderes Terminal/Monitor-Fenster den seriellen Port blockiert — alle anderen Terminals schließen und nochmal versuchen.
Reines Ladekabel statt Datenkabel angeschlossen → anderes Kabel probieren.
Rote LED am Board leuchtet dauerhaft beim Anstecken → normal, das ist die eingebaute Ladeanzeige, kein Fehler.
4. Meshtastic-Node per CLI vorbereiten

Die Node muss einmalig per USB direkt am Rechner konfiguriert werden (die spätere D6/D7-Verbindung reicht dafür nicht aus).

Meshtastic-CLI installieren:

pip3 install --upgrade meshtastic

Node per USB anschließen, dann:

meshtastic --port /dev/cu.xxxxx --info

(Portname mit meshtastic --info ohne --port herausfinden, falls nur ein Gerät angeschlossen ist)

Region setzen (Pflicht — ohne das sendet die Node gar nicht):

meshtastic --port /dev/cu.xxxxx --set lora.region EU_868

Serielle Schnittstelle für die Verbindung zum ESP32-S3 aktivieren:

meshtastic --port /dev/cu.xxxxx --set serial.enabled true
meshtastic --port /dev/cu.xxxxx --set serial.mode PROTO
meshtastic --port /dev/cu.xxxxx --set serial.rxd 7
meshtastic --port /dev/cu.xxxxx --set serial.txd 6
meshtastic --port /dev/cu.xxxxx --set serial.baud BAUD_115200

Danach USB-Kabel von der Node abziehen und stattdessen auf die D6/D7-Verkabelung zum ESP32-S3 umstecken.

Wie der Code funktioniert

Die komplette Logik steckt in src/main.cpp:

mt_serial_init(...) baut beim Start die Verbindung zur Meshtastic-Node auf.
mt_loop(millis()) muss in jedem Durchlauf der loop()-Funktion aufgerufen werden — das hält die Kommunikation mit der Node am Laufen.
Alle 20 Sekunden wird mt_send_text(...) aufgerufen und schickt eine Textnachricht als Broadcast ins Mesh.
Die eingebaute LED (GPIO21) zeigt zwei Zustände:
Langsames Blinken (alle 500ms) im Ruhezustand → "Programm läuft"
Schnelles Blinken (6× kurz) beim Senden einer Nachricht → "Nachricht geht gerade raus"
Testen, ob's funktioniert
Seriellen Monitor öffnen: PlatformIO-Seitenleiste → Monitor, oder pio run --target monitor
Nach dem Start sollte >>> VERBUNDEN mit der Node erscheinen
Alle 20 Sekunden: >>> Nachricht gesendet, Erfolg: JA
Zur Kontrolle die Meshtastic-App oder den öffentlichen Kanal eines zweiten Geräts im Mesh beobachten
Troubleshooting-Schnellübersicht
Problem	Wahrscheinlichste Ursache
Upload schlägt fehl ("serial noise", Timeout)	Anderes Terminal/Monitor blockiert den Port, oder Ladekabel statt Datenkabel
>>> VERBUNDEN erscheint nie	Verkabelung prüfen (TX/RX gekreuzt? GND verbunden?), Baudrate auf beiden Seiten identisch?
Verbindung da, aber keine Nachricht im Mesh	Region der Node gesetzt? txEnabled: true? Richtiger Kanal beobachtet?
Rote LED leuchtet permanent	Normal — eingebaute Ladeanzeige, kein Fehler
IntelliSense zeigt rote Fehler im Editor, Build läuft aber durch	Bekannter clang/Xtensa-Konflikt in der Editor-Vorschau, kein echter Fehler
Lizenz

Siehe LICENSE.
