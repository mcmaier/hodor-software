# Architektur – HODOR

Überblick über Systemblöcke, Datenflüsse und Kern-Aufteilung.  
Details zu Zuständen → `state_machine.md` | Regler → `control_loop.md` | Pins → `hardware.md`

---

## 1  Systemübersicht

```
┌─────────────────────────────────────────────────────────────────┐
│                        ESP32-S3                                 │
│                                                                 │
│  ┌──────────────────────────┐  ┌──────────────────────────┐    │
│  │        Core 0            │  │        Core 1            │    │
│  │  WiFi / Kommunikation    │  │  Steuerung / Regelung    │    │
│  │                          │  │                          │    │
│  │  ┌──────────────────┐    │  │  ┌──────────────────┐   │    │
│  │  │   WiFi Stack     │    │  │  │  System-SM       │   │    │
│  │  └────────┬─────────┘    │  │  └────────┬─────────┘   │    │
│  │           │              │  │           │              │    │
│  │  ┌────────▼─────────┐    │  │  ┌────────▼─────────┐   │    │
│  │  │   MQTT Client    │◄───┼──┼──│  Tür-SM          │   │    │
│  │  └──────────────────┘    │  │  └────────┬─────────┘   │    │
│  │  ┌──────────────────┐    │  │           │              │    │
│  │  │   HTTP Webserver │    │  │  ┌────────▼─────────┐   │    │
│  │  └──────────────────┘    │  │  │  Kaskadenregler  │   │    │
│  │                          │  │  └────────┬─────────┘   │    │
│  │  ┌──────────────────┐    │  │           │              │    │
│  │  │   NVS Config     │◄───┼──┼──►  cfg_get/set()   │   │    │
│  │  └──────────────────┘    │  │           │              │    │
│  └──────────────────────────┘  └───────────┼──────────────┘    │
│                                            │                   │
└────────────────────────────────────────────┼───────────────────┘
                                             │
             ┌───────────────────────────────┼───────────────────┐
             │              HAL              │                   │
             │                 ┌─────────┐  ┌▼──────────┐        │
             │                 │ ACS725  │  │  H-Brücke │        │
             │                 │(Strom)  │  │  + PWM    │        │
             │                 └─────────┘  └───────────┘        │
             │  ┌──────────┐  ┌─────────┐  ┌───────────┐         │
             │  │ Encoder /│  │  I/O    │  │ 74LVC1G123│         │
             │  │Endschalter│ │(pot.fr.)│  │ Watchdog  │         │
             │  └──────────┘  └─────────┘  └───────────┘         │
             └───────────────────────────────────────────────────┘
```

---

## 2  Modul-Übersicht

| Modul | Verzeichnis | Kern | Aufgabe |
|-------|-------------|------|---------|
| System-SM | `state_machine/` | 1 | Betriebszustand; Event-Dispatch |
| Tür-SM | `door/` | 1 | Tür-Ablauf; Blockierungs-Retry |
| Kaskadenregler | `control/` | 1 | Position → Geschwindigkeit → Strom → PWM |
| Motoransteuerung | `motor/` | 1 | H-Brücke, PWM-Ausgabe, Watchdog-Trigger |
| Sensorik | `sensing/` | 1 | ACS725, Encoder/Endschalter |
| I/O | `io/` | 1 | Eingänge (Taster/Schalter/Melder), Ausgang |
| Kommunikation | `comm/` | 0 | WiFi, MQTT, HTTP-Webserver |
| Konfiguration | `config/` | 0+1 | NVS lesen/schreiben; gemeinsam genutzt |
| HAL | `hal/` | — | Hardware-Abstraktion; keine Logik |

---

## 3  Datenflüsse

### 3.1  Steuerungspfad (Core 1, zeitkritisch)

```
Endschalter/Encoder
        │  (ISR → Queue)
        ▼
    sensing_task
        │  sns_data_t
        ▼
    control_task  ◄─── Sollwert von Tür-SM
        │  mot_cmd_t (PWM-Duty, Richtung)
        ▼
    motor_task
        │  GPIO / LEDC
        ▼
    H-Brücke → Motor
```

### 3.2  Kommunikationspfad (Core 0, nicht zeitkritisch)

```
MQTT Broker
    │  JSON Payload
    ▼
comm_task  ──► Event-Queue (Core 1)
    ▲
    │  Statusmeldungen
    │
HTTP Webserver ──► cfg_set()  ──► NVS
```

### 3.3  Konfigurationspfad (beide Cores)

```
NVS (Flash)
    │
cfg_get()  ──► alle Module (read-only zur Laufzeit)
    ▲
cfg_set()  ──► nur über Webserver / MQTT (Core 0)
              (Mutex-gesichert)
```

---

## 4  Inter-Task-Kommunikation

| Sender | Empfänger | Mechanismus | Typ |
|--------|-----------|-------------|-----|
| ISR (Encoder/Endschalter) | `sensing_task` | `xQueueSendFromISR()` | `isr_event_t` |
| `sensing_task` | `control_task` | Queue | `sns_data_t` |
| `control_task` | `motor_task` | Queue | `mot_cmd_t` |
| `comm_task` | `sm_system` | Queue | `sm_event_t` |
| `io_task` | `sm_system` | Queue | `sm_event_t` |
| `sm_door` | `sm_system` | Direktaufruf (gleicher Core) | `sm_event_t` |
| `config` | alle | Mutex + gemeinsamer Struct | `hodor_config_t` |

> **Regel:** Niemals direkte Funktionsaufrufe zwischen Core-0- und Core-1-Tasks. Immer Queue oder Mutex.

---

## 5  Speicheraufteilung

| Bereich | Inhalt | Ablageort |
|---------|--------|-----------|
| Flash (NVS) | Konfigurationsparameter | `nvs_flash` Partition |
| IRAM | ISR-Handler, zeitkritische Regelpfade | `IRAM_ATTR` |
| DRAM | Task-Stacks, Queues, Puffer | Heap (statisch alloziert) |
| RTC | — | nicht genutzt (kein Deep Sleep) |

> **Regel:** Keine dynamische Allokation (`malloc`) im Steuerungspfad nach `SYS_INIT`.

---

## 6  Externe Hardware-Anbindung

| Peripheral | Interface | Takt / Auflösung | Anmerkung |
|------------|-----------|-----------------|-----------|
| ACS725 (Strom) | Analog → ADC | — | Midpoint ~1,5 V; bidirektional |
| Spannung | Analog → ADC | — | Midpoint ~1,5 V; bidirektional |
| H-Brücke | GPIO + LEDC (PWM) | 20 kHz | Frequenz in `hodor_config.h` |
| Encoder A/B | GPIO (PCNT) | — | Alternativ: Endschalter |
| Endschalter | GPIO (Pull-up) | — | Entprellung in HAL |
| Pot.fr. Eingänge | GPIO  | — | Konfigurierbar als Taster/Schalter/Melder |
| Relais | GPIO | — |  Konfigurierbarer Einschaltzeitpunkt |
| 74LVC1G123 | GPIO (Retriggerpuls) | 10 ms | Timeout → Motor-Abschaltung |

**Pinbelegung:** → `docs/hardware.md`

---

## 7  Nicht im Scope (v1.0)

- OTA-Updates
- TLS/HTTPS für Webserver
- Mehrere Türen pro Controller
- Ruckbegrenzte Regelung (optional, siehe `control_loop.md`)
