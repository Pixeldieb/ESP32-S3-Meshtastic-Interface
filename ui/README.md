# UI-Dokumentation

Diese Dokumentation beschreibt die Benutzeroberfläche des ESP32-Notfallterminals.

Die UI ist so dokumentiert, dass sowohl Menschen als auch KI-Agenten die
Menüstruktur, Seiten, Bedienelemente und Bedienabläufe eindeutig verstehen
können.

---

## Dateien

| Datei | Zweck |
|---|---|
| `ui.yaml` | Technische Quelle der Wahrheit für die komplette UI |
| `flows.yaml` | Beschreibt konkrete Bedien- und Notfallabläufe |
| `README.md` | Erklärung der UI-Struktur und Konventionen |

### Quelle der Wahrheit

`ui.yaml` ist die maßgebliche Datei für:

- vorhandene Seiten
- Seiten-IDs
- UI-Elemente
- Button-IDs
- Navigation
- Aktionen
- Zustände
- Hardware-Anzeigen

`flows.yaml` ergänzt diese Informationen um konkrete Bedienabläufe.

---

## 1. Grundprinzip

Das Terminal verfügt über ein Hauptmenü, das nur während eines
Stromausfalls verfügbar ist.

Im aktiven Zustand signalisiert eine leicht pulsierende Signal-LED,
dass das Terminal verfügbar ist.

Das Hauptmenü besteht aus vier Bereichen:

```
Hauptmenü
├── Feuerwehr
├── Polizei
├── Krankenwagen
└── Info
```

## 2. Hauptmenü

Page ID: `main_menu`

Das Hauptmenü ist nur verfügbar, wenn:

```
system.power_failure == true
```

Bei aktiviertem Hauptmenü pulsiert die `signal_led` leicht.

### Auswahl

| Anzeige | ID | Ziel |
|---|---|---|
| Feuerwehr | `fire_department` | `fire_department_menu` |
| Polizei | `police` | `police_menu` |
| Krankenwagen | `ambulance` | `ambulance_menu` |
| Info | `information` | `information_menu` |

## 3. Feuerwehr

Page ID: `fire_department_menu`

```
Feuerwehr
├── Brand
├── Chemie
├── Verkehrsunfall
└── Andere
```

Jede Auswahl führt zur Seite `emergency_confirmation`. Dabei werden die
Informationen zum gewählten Notfall an die Bestätigungsseite übergeben.

Beispiel:

```
Feuerwehr
└── Brand
    └── Notfall bestätigen
```

## 4. Polizei

Page ID: `police_menu`

```
Polizei
├── Einbruch
├── Diebstahl
├── Sicherheit
└── Andere
```

Jede Auswahl führt zu `emergency_confirmation`.

## 5. Krankenwagen

Page ID: `ambulance_menu`

```
Krankenwagen
├── Verletzung
├── Notarzt
├── Transport
└── Andere
```

Jede Auswahl führt zu `emergency_confirmation`.

## 6. Notfall bestätigen

Page ID: `emergency_confirmation`

Vor dem Absenden eines Notfalls wird immer eine Bestätigungsseite angezeigt.

```
Notfall bestätigen

[Abbruch]    [Bestätigung]
```

**Abbruch** — ID: `cancel`, Aktion: `back`. Der Notfall wird nicht ausgelöst.

**Bestätigung** — ID: `confirm`, Aktion: `trigger_emergency`. Danach wird die
Notfallübertragung gestartet.

## 7. Notfallübertragung

Page ID: `emergency_transmission`

Nach der Bestätigung:

1. Die Warn-LED wird aktiviert.
2. Eine Meshtastic-Nachricht wird erzeugt.
3. Die Nachricht wird über Meshtastic übertragen.
4. Der Übertragungsstatus wird angezeigt.

Bei erfolgreicher Übertragung: `emergency_details`
Bei fehlgeschlagener Übertragung: `emergency_transmission_failed`

## 8. Erfolgreiche Übertragung

Page ID: `emergency_details`

Nach erfolgreicher Übertragung werden die Notfalldetails auf dem Display
angezeigt. Diese Informationen sollen vom Benutzer notiert werden können.

Die angezeigten Daten stammen aus `emergency.last_transmission`.

## 9. Fehlgeschlagene Übertragung

Page ID: `emergency_transmission_failed`

Bei einem Übertragungsfehler werden zwei Möglichkeiten angeboten:

```
Übertragung fehlgeschlagen

[Erneut senden]    [Abbruch]
```

**Erneut senden** — startet die Übertragung erneut.
**Abbruch** — kehrt zum Hauptmenü zurück.

## 10. Info

Page ID: `information_menu`

```
Info
├── Lageinformationen anfordern
└── Systeminformationen Krisenstab
```

## 11. Lageinformationen

Page ID: `situation_information`

Zeigt aktuelle Lageinformationen und letzte empfangene Meldungen. Die
beiden rechten Bedienelemente dienen zum Scrollen.

```
┌──────────────────────────────┐
│ Lageinformationen            │
│                              │
│ Letzte Meldungen             │
│                              │
│ Meldung 1                    │
│ Meldung 2                    │
│ Meldung 3                    │
│                              │
│ [Hoch] [Runter] [Zurück]     │
└──────────────────────────────┘
```

Die Meldungen werden aus `situation_information.messages` geladen.

## 12. Systeminformationen Krisenstab

Page ID: `crisis_staff_information`

Zeigt Informationen und letzte Meldungen für den Krisenstab. Auch hier
dienen die beiden rechten Bedienelemente zum Scrollen.

```
┌──────────────────────────────┐
│ Systeminformationen          │
│ Krisenstab                   │
│                              │
│ Letzte Meldungen             │
│                              │
│ Meldung 1                    │
│ Meldung 2                    │
│ Meldung 3                    │
│                              │
│ [Hoch] [Runter] [Zurück]     │
└──────────────────────────────┘
```

Die Meldungen werden aus `crisis_staff_information.messages` geladen.

## 13. Navigationsregeln

### Zurück

Wenn eine Seite mit:

```yaml
action:
  type: back
```

definiert ist, wird der vorherige Navigationszustand wiederhergestellt.
Die Firmware sollte dafür einen Navigations-Stack verwenden.

Beispiel:

```
main_menu
→ fire_department_menu
→ emergency_confirmation
```

Nach Abbruch:

```
emergency_confirmation
→ fire_department_menu
```

## 14. IDs

Jede Seite und jedes interaktive Element besitzt eine eindeutige ID.

Beispiele:

```
main_menu
fire_department_menu
police_menu
ambulance_menu
information_menu

fire_department
police
ambulance
information

confirm
cancel
```

IDs sind technische Bezeichner und sollten stabil bleiben. Die sichtbaren
Texte dürfen sich dagegen ändern.

Beispiel:

```yaml
id: fire_department
label: "Feuerwehr"
```

Die Beschriftung kann geändert werden, ohne dass sich die technische ID
ändert.

## 15. Aktionen

Die UI verwendet standardisierte Aktionstypen.

| Aktion | Bedeutung |
|---|---|
| `navigate` | Zu einer anderen Seite wechseln |
| `back` | Zur vorherigen Seite zurückkehren |
| `execute` | Eine Firmware-Funktion ausführen |
| `scroll` | Inhalt scrollen |
| `view` | Inhalt anzeigen |

## 16. KI-Agenten

KI-Agenten sollen bei Änderungen an der UI zuerst `ui.yaml` lesen.

### Neue Seite

Beim Hinzufügen einer Seite müssen mindestens angegeben werden:

```yaml
- id: unique_page_id
  title: "Seitentitel"
  elements: []
```

### Neues interaktives Element

Jedes interaktive Element benötigt:

```yaml
- id: unique_element_id
  type: button
  label: "Anzeigename"
  action:
    type: ...
```

### Neue Navigation

Navigation muss über eine explizite Aktion dokumentiert werden:

```yaml
action:
  type: navigate
  target: target_page
```

### Keine implizite Navigation

Die Firmware soll nicht voraussetzen, dass ein Agent aus einem
Button-Label die Zielseite erraten muss.

Nicht:

```yaml
label: "WLAN"
```

Sondern:

```yaml
id: wifi_settings
label: "WLAN"
action:
  type: navigate
  target: wifi_settings
```

## 17. Stabilität der Dokumentation

Die UI-Dokumentation wird als Schnittstelle zwischen Firmware, Entwicklung
und KI-Agenten betrachtet.

Daher gilt: IDs sind stabiler als sichtbare Beschriftungen.

Eine Änderung wie:

```
"Feuerwehr" → "Feuerwehr / Brandbekämpfung"
```

ändert nicht die ID `fire_department`.

Eine Änderung der ID muss dagegen alle Referenzen in `ui.yaml`,
`flows.yaml`, Firmware, Tests und eventuell weiteren
Dokumentationsdateien berücksichtigen.

## 18. Bedienbaum

Die vollständige Menüstruktur lässt sich vereinfacht so darstellen:

```
Hauptmenü
│
├── Feuerwehr
│   ├── Brand
│   │   └── Notfall bestätigen
│   │       ├── Abbruch
│   │       └── Bestätigung
│   │           └── Meshtastic
│   │               └── Notfalldetails
│   │
│   ├── Chemie
│   │   └── Notfall bestätigen
│   │
│   ├── Verkehrsunfall
│   │   └── Notfall bestätigen
│   │
│   └── Andere
│       └── Notfall bestätigen
│
├── Polizei
│   ├── Einbruch
│   │   └── Notfall bestätigen
│   ├── Diebstahl
│   │   └── Notfall bestätigen
│   ├── Sicherheit
│   │   └── Notfall bestätigen
│   └── Andere
│       └── Notfall bestätigen
│
├── Krankenwagen
│   ├── Verletzung
│   │   └── Notfall bestätigen
│   ├── Notarzt
│   │   └── Notfall bestätigen
│   ├── Transport
│   │   └── Notfall bestätigen
│   └── Andere
│       └── Notfall bestätigen
│
└── Info
    ├── Lageinformationen anfordern
    │   └── Lageinformationen
    │       ├── Scroll hoch
    │       ├── Scroll runter
    │       └── Zurück
    │
    └── Systeminformationen Krisenstab
        └── Lageinformationen
            ├── Scroll hoch
            ├── Scroll runter
            └── Zurück
```

## Hinweis zur weiteren Entwicklung

Die drei Dateien sind bewusst so getrennt:

- `ui.yaml` → Was existiert?
- `flows.yaml` → Wie benutzt man es?
- `README.md` → Wie ist das System aufgebaut und welche Regeln gelten?

Das ist für einen KI-Agenten eine gute Trennung, weil er beispielsweise bei
einer Änderung am „Brand“-Button gezielt `ui.yaml` durchsuchen kann,
während er für einen kompletten Notrufablauf `flows.yaml` heranzieht.
