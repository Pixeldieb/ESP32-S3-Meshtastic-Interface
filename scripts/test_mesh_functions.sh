#!/usr/bin/env bash
#
# Sendet eine Testsequenz aller aktuell unterstützten Mesh-Kommandos
# (Zustandswechsel-Codes + Lagemeldungen) über einen angeschlossenen
# Meshtastic-Node. Ergebnis manuell im seriellen Monitor des Brain-Boards
# prüfen (status / liste / detail <ID>).
#
# Voraussetzung: meshtastic-CLI installiert (pip3 install --upgrade meshtastic),
# ein zweiter Meshtastic-Node per USB angeschlossen, im selben Kanal wie
# der Brain-Node.

set -euo pipefail

readonly SCRIPT_NAME="$(basename "$0")"
readonly DEFAULT_DELAY_SECONDS=3

usage() {
  cat <<EOF
Verwendung: ${SCRIPT_NAME} --port <serieller-port> [--delay <sekunden>]

Optionen:
  --port <path>      Serieller Port des sendenden Meshtastic-Node
                      (z.B. /dev/cu.usbmodem2101)
  --delay <sekunden>  Wartezeit zwischen den Nachrichten (Default: ${DEFAULT_DELAY_SECONDS})
  -h, --help          Diese Hilfe anzeigen

Beispiel:
  ${SCRIPT_NAME} --port /dev/cu.usbmodem2101
EOF
}

port=""
delay="${DEFAULT_DELAY_SECONDS}"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --port)
      port="${2:-}"
      shift 2
      ;;
    --delay)
      delay="${2:-}"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unbekannte Option: $1" >&2
      usage
      exit 1
      ;;
  esac
done

if [[ -z "${port}" ]]; then
  echo "Fehler: --port ist erforderlich." >&2
  usage
  exit 1
fi

if ! command -v meshtastic >/dev/null 2>&1; then
  echo "Fehler: 'meshtastic' CLI nicht gefunden. Installation: pip3 install --upgrade meshtastic" >&2
  exit 1
fi

if [[ ! -e "${port}" ]]; then
  echo "Fehler: Port '${port}' existiert nicht." >&2
  exit 1
fi

send_message() {
  local message="$1"
  local description="$2"
  echo ">>> Sende '${message}' (${description})"
  meshtastic --port "${port}" --sendtext "${message}"
  sleep "${delay}"
}

echo "=== Test 1: Zustandswechsel-Codes ==="
send_message "LGE"  "Lageoeffnung -> AKTIV"
send_message "WTG"  "Wartung -> WARTUNG"
send_message "SAB"  "Sabotage-Test -> SABOTAGE"
send_message "SAUS" "Stromausfall-Test -> STROMAUSFALL"
send_message "NOR"  "Normalbetrieb -> STANDBY"

echo "=== Test 2: Lagemeldungen ==="
send_message "LAGE:NEU;Brand;offen;Testmeldung ueber ${SCRIPT_NAME}" "neue Lagemeldung anlegen"
send_message "LAGE:1;Brand;in Bearbeitung;Update ueber ${SCRIPT_NAME}" "Lagemeldung 1 aktualisieren"
send_message "LAGE:9999;Brand;offen;Test nicht existente ID" "Update mit unbekannter ID (erwartet: Fehlermeldung)"

echo "=== Fertig. Am Brain-Node im seriellen Monitor pruefen: 'status', 'liste', 'detail 1' ==="
