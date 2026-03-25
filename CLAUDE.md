# HODOR – Hold/Open Door Operation Regulator

Motorcontroller für Schiebetüren auf Basis des ESP32-S3.

## Projektstruktur

```
hodor/
├── CLAUDE.md
├── CMakeLists.txt
├── sdkconfig
├── main/
│   ├── CMakeLists.txt
│   ├── main.c                  # App-Einstieg, Core-Zuweisung
│   ├── state_machine/          # System-Statemachine (Start/Init/Standby/Active/Error)
│   ├── door/                   # Tür-Zustände und Ablaufsteuerung
│   ├── control/                # Kaskadenregler (Position → Geschwindigkeit → Strom → PWM)
│   ├── motor/                  # H-Brücke, PWM-Ausgabe, Watchdog-Trigger
│   ├── sensing/                # Strommessung (ACS723 + ADS8860), Encoder/Endschalter
│   ├── io/                     # Potentialfreie Eingänge, schaltbarer 12/24V-Ausgang
│   ├── comm/                   # WiFi, MQTT, Webserver (Core 0)
│   ├── config/                 # NVS-Parameter, persistente Konfiguration
│   └── hal/                    # Hardware-Abstraktionsschicht
├── components/                 # Eigene ESP-IDF-Komponenten
└── docs/
    ├── architecture.md
    ├── state_machine.md
    ├── control_loop.md
    └── hardware.md
```

## Tech Stack

- **MCU:** ESP32-S3
- **Framework:** ESP-IDF (neueste stabile Version)
- **Sprache:** C (C11)
- **Build:** CMake via ESP-IDF
- **Protokolle:** MQTT, HTTP (Webserver), NVS

## Architektur

### Dual-Core-Aufteilung
- **Core 0:** Kommunikation – WiFi, MQTT, HTTP-Webserver
- **Core 1:** Steuerung – State Machine, Regler, I/O

### Kaskadenregelung (Core 1)
```
Sollposition
    └─► Positionsrampe (ruckbegrenzt, optional)
            └─► Geschwindigkeitsregler (PI)
                    └─► Stromregler (PI, schnell)
                            └─► PWM → H-Brücke
```

### System-Statemachine
```
Start → Init → Standby → Aktiv → Fehler
                  ↑          |
                  └──────────┘ (nach Fehlerbehandlung)
```

### Tür-Zustände
`Closed → Opening → Open → Closing → Closed`  
Zusatz: `Blocked`, `Error`, `Undefined`

#### Zustands-Verknüpfung
```
| System State | Mögicher Türzustand |
|---|---|
| Init | Undefined |
| Standby | Closed, Open, Undefined, Blocked |
| Aktiv | Opening, Closing |
| Error | Error |
```

## Hardware-Referenz

| Komponente | Funktion |
|---|---|
| ACS725 | Bidirektionale Strommessung (Midpoint ~1,5 V) |
| 74LVC1G123 | Hardware-Watchdog: Motor-Aus bei Controller-Ausfall |
| H-Brücke (extern) | DC-Motor 12–24 V / max. 5 A |

- Zwei Quadraturencoder-Eingänge **oder** Endschalter (konfigurierbar)
- 3 konfigurierbare Potentialfreie Steuereingänge (Taster, Schalter, Bewegungsmelder, …)
- Ein schaltbares Relais als potentialfreier Ausgang

## Coding-Regeln

- Schreibe C11; kein C++
- Jede Datei hat einen einzigen Verantwortungsbereich (SRP)
- Präfixe pro Modul: `sm_`, `door_`, `ctrl_`, `mot_`, `sns_`, `io_`, `comm_`, `cfg_`, `param_`
- Fehlerbehandlung: `esp_err_t` durchgängig; kein Ignorieren von Rückgabewerten
- FreeRTOS-Tasks nur in `main.c` oder `comm/` starten; Prio-Schema kommentieren
- ISR-Routinen minimal halten; Daten via Queue/Semaphore übergeben
- Kritische Regelpfade (Core 1) **ohne** dynamische Allokation zur Laufzeit
- Magic Numbers verboten: Konstanten in `hodor_config.h`
- **Kein direkter Variablenzugriff auf Parameter** – ausschließlich `param_get()` / `param_set()` aus `config/hodor_param.h`; keine Modul-eigenen Kopien von Parameterwerten

## Build & Flash

```bash
# Einmalig
. $IDF_PATH/export.sh
idf.py set-target esp32s3

# Täglich
idf.py build
idf.py flash monitor
idf.py flash monitor -p /dev/ttyUSB0   # Port anpassen

# Tests (Unity)
idf.py -C test build flash monitor
```

## Parameter-Framework

Alle Parameter leben in `param_table[]` (`config/hodor_param.h`).  
Zugriff: `param_get(id, &val)` / `param_set(id, val)` – nie direkter Struct-Zugriff.  
Vollständige Parameter-Liste, IDs, Typen, Limits und UART/JSON-Protokoll → `docs/parameters.md`

## Bekannte Design-Entscheidungen

- **Keine** Low-Side-Shunts: nur ACS725 liefert Richtungsinformation (nötig ohne Encoder)
- **ADC** ESP32-S3 ADC Sample-Trigger PWM-synchron - bekannte Verbesserungsmaßnahmen für ESP ADC implementieren
- **Hardware-Watchdog** (74LVC1G123): unabhängig vom Software-Watchdog, schaltet Motorpfad ab - regelmäßiger Trigger erforderlich
- **LM74610** (Ideal-Diode): Bootstrap-Prevention bei manuellem Türbewegen

## Weiterführende Docs

- `docs/architecture.md` – Gesamtarchitektur, Blockschaltbild
- `docs/state_machine.md` – Zustände, Transitionen, Guards
- `docs/control_loop.md` – Reglerparametrierung, Abtastzeiten
- `docs/hardware.md` – Schaltplan-Referenz, Pinbelegung ESP32-S3
- `docs/parameters.md` – Parameter-IDs, Typen, Limits, UART/JSON-Protokoll