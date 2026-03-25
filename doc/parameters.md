# Parameter-Framework – HODOR

Spezifikation für alle Laufzeit- und Konfigurationsparameter.  
Ziel: kein freier Variablen-Zugriff, einheitliche Validierung, UART-Ausgabe ohne Zusatzaufwand.  
Implementierung → `config/hodor_param.h` + `config/hodor_param.c`

---

## 1  Konzept

Jeder Parameter existiert genau einmal – als Eintrag in der **Parameter-Tabelle** (`param_table[]`).  
Die Tabelle ist die einzige Quelle für:

- aktuellen Wert (RAM)
- Defaultwert (Flash / ROM)
- Limits (min / max)
- Typ und ID
- Name-String (für UART/JSON)
- NVS-Schlüssel (für Persistenz)

Kein Modul hält eigene Kopien von Parameterwerten. Zugriff ausschließlich über `param_get()` / `param_set()`.

---

## 2  Parameter-Typen

| Enum `param_type_t` | C-Typ | Größe | Verwendung |
|---------------------|-------|-------|------------|
| `PARAM_TYPE_FLOAT`  | `float` | 4 Byte | Regler-Koeffizienten, Ströme, Geschwindigkeiten |
| `PARAM_TYPE_UINT16` | `uint16_t` | 2 Byte | Positionen [mm], Zeiten [ms] |
| `PARAM_TYPE_UINT8`  | `uint8_t` | 1 Byte | Modi, Eingangs-Konfiguration |
| `PARAM_TYPE_BOOL`   | `uint8_t` (0/1) | 1 Byte | Enable/Disable-Flags |

Interner Wert immer in `param_value_t` (Union über alle Typen) gespeichert.

---

## 3  Parameter-Deskriptor

Jeder Eintrag in `param_table[]` ist ein `param_desc_t`:

| Feld | Typ | Beschreibung |
|------|-----|--------------|
| `id` | `uint16_t` | Eindeutige Parameter-ID (Enum `param_id_t`) |
| `type` | `param_type_t` | Datentyp |
| `name` | `const char *` | Kurzname für UART/JSON (max. 24 Zeichen) |
| `unit` | `const char *` | Einheit als String (z.B. `"A"`, `"mm"`, `"-"`) |
| `nvs_key` | `const char *` | NVS-Schlüssel (max. 15 Zeichen, NVS-Limit) |
| `val` | `param_value_t` | Aktueller Wert (RAM) |
| `def` | `param_value_t` | Defaultwert (wird bei Reset geladen) |
| `min` | `param_value_t` | Untere Grenze (inklusiv) |
| `max` | `param_value_t` | Obere Grenze (inklusiv) |
| `flags` | `uint8_t` | Siehe Abschnitt 4 |

Gesamtgröße pro Deskriptor: ~64 Byte (abhängig von Compiler-Padding).

---

## 4  Flags

| Bit | Maske | Bedeutung |
|-----|-------|-----------|
| 0 | `PARAM_FLAG_PERSIST` | In NVS speichern / laden |
| 1 | `PARAM_FLAG_READONLY` | Nur lesbar (z.B. gemessene Werte) |
| 2 | `PARAM_FLAG_STREAM`   | In UART-Telemetrie-Stream einschließen |
| 3 | `PARAM_FLAG_REBOOT`   | Änderung erst nach Neustart wirksam |
| 4–7 | — | Reserviert |

---

## 5  Parameter-IDs (`param_id_t`)

IDs in Blöcken gruppiert – Lücken absichtlich lassen für spätere Erweiterungen.

### Block 0x0000 – Motor / Leistung

| ID | Name | Typ | Einheit | Default | Min | Max |
|----|------|-----|---------|---------|-----|-----|
| `0x0001` | `motor_max_a` | float | A | 3.0 | 0.1 | 5.0 |
| `0x0002` | `motor_pwm_freq_hz` | uint16 | Hz | 20000 | 10000 | 40000 |
| `0x0003` | `overcurrent_ms` | uint16 | ms | 200 | 10 | 2000 |

### Block 0x0100 – Tür / Position

| ID | Name | Typ | Einheit | Default | Min | Max |
|----|------|-----|---------|---------|-----|-----|
| `0x0101` | `door_open_mm` | uint16 | mm | 800 | 50 | 2000 |
| `0x0102` | `pos_tolerance_mm` | uint16 | mm | 5 | 1 | 50 |
| `0x0103` | `autoclose_s` | uint16 | s | 0 | 0 | 300 |

### Block 0x0200 – Regler

| ID | Name | Typ | Einheit | Default | Min | Max |
|----|------|-----|---------|---------|-----|-----|
| `0x0201` | `ctrl_i_kp` | float | - | TBD | 0.0 | 100.0 |
| `0x0202` | `ctrl_i_ki` | float | - | TBD | 0.0 | 1000.0 |
| `0x0203` | `ctrl_v_kp` | float | - | TBD | 0.0 | 100.0 |
| `0x0204` | `ctrl_v_ki` | float | - | TBD | 0.0 | 1000.0 |
| `0x0205` | `ctrl_p_kp` | float | - | TBD | 0.0 | 10.0 |
| `0x0206` | `v_max_mms` | float | mm/s | TBD | 10.0 | 500.0 |
| `0x0207` | `v_min_thresh_mms` | float | mm/s | 2.0 | 0.5 | 20.0 |

### Block 0x0300 – Ruckbegrenzer (optional)

| ID | Name | Typ | Einheit | Default | Min | Max |
|----|------|-----|---------|---------|-----|-----|
| `0x0301` | `jerk_limit_en` | bool | - | false | - | - |
| `0x0302` | `ramp_jerk` | float | mm/s³ | TBD | 1.0 | 10000.0 |
| `0x0303` | `ramp_accel_max` | float | mm/s² | TBD | 1.0 | 1000.0 |

### Block 0x0400 – I/O-Konfiguration

| ID | Name | Typ | Einheit | Default | Min | Max |
|----|------|-----|---------|---------|-----|-----|
| `0x0401` | `input_mode_1` | uint8 | - | 0 | 0 | 3 |
| `0x0402` | `input_mode_2` | uint8 | - | 0 | 0 | 3 |
| `0x0403` | `input_mode_3` | uint8 | - | 0 | 0 | 3 |
| `0x0404` | `input_mode_4` | uint8 | - | 0 | 0 | 3 |

### Block 0x0500 – Kommunikation

| ID | Name | Typ | Einheit | Default | Min | Max |
|----|------|-----|---------|---------|-----|-----|
| `0x0501` | `mqtt_en` | bool | - | false | - | - |
| `0x0502` | `uart_stream_ms` | uint16 | ms | 100 | 10 | 5000 |

### Block 0x0F00 – Telemetrie (Read-only, kein NVS)

Messwerte, die über UART gestreamt werden können. Kein `PARAM_FLAG_PERSIST`.

| ID | Name | Typ | Einheit |
|----|------|-----|---------|
| `0x0F01` | `meas_current_a` | float | A |
| `0x0F02` | `meas_velocity_mms` | float | mm/s |
| `0x0F03` | `meas_position_mm` | float | mm |
| `0x0F04` | `meas_pwm_duty_pct` | float | % |
| `0x0F05` | `meas_sys_state` | uint8 | - |
| `0x0F06` | `meas_door_state` | uint8 | - |

---

## 6  Zugriffs-API

| Funktion | Beschreibung |
|----------|--------------|
| `param_init()` | Tabelle initialisieren; NVS-Werte laden; Defaults setzen falls NVS leer |
| `param_get(id, *val)` | Wert lesen; gibt `ESP_ERR_NOT_FOUND` wenn ID unbekannt |
| `param_set(id, val)` | Wert schreiben mit Limit-Prüfung; gibt `ESP_ERR_INVALID_ARG` bei Verletzung |
| `param_set_default(id)` | Einzelnen Parameter auf Default zurücksetzen |
| `param_reset_all()` | Alle Parameter auf Default; NVS löschen |
| `param_save(id)` | Einzelnen Parameter in NVS schreiben (nur wenn `PARAM_FLAG_PERSIST`) |
| `param_save_all()` | Alle persistenten Parameter in NVS schreiben |
| `param_get_desc(id)` | Pointer auf `param_desc_t` (für UART/JSON-Ausgabe) |

**Regeln:**
- `param_set()` prüft immer `min` ≤ `val` ≤ `max` vor dem Schreiben
- `PARAM_FLAG_READONLY` → `param_set()` gibt `ESP_ERR_NOT_SUPPORTED` zurück
- Mutex-Schutz in `param_set()` / `param_get()` für Core-0/Core-1-Zugriff

---

## 7  UART / JSON-Protokoll

### 7.1  Einzelabfrage (Request/Response)

Request (Host → HODOR):
```json
{"cmd": "get", "id": "0x0201"}
```

Response (HODOR → Host):
```json
{"id": "0x0201", "name": "ctrl_i_kp", "val": 1.25, "unit": "-", "min": 0.0, "max": 100.0}
```

Setzen:
```json
{"cmd": "set", "id": "0x0201", "val": 1.5}
```

Response bei Erfolg:
```json
{"id": "0x0201", "ok": true, "val": 1.5}
```

Response bei Fehler:
```json
{"id": "0x0201", "ok": false, "err": "out_of_range"}
```

Alle Parameter auflisten:
```json
{"cmd": "list"}
```

### 7.2  Telemetrie-Stream

Alle Parameter mit `PARAM_FLAG_STREAM` werden zyklisch gesendet (Periode = `uart_stream_ms`).  
Format: kompakte JSON-Zeile, newline-terminiert – direkt in Serial-Plotter / Python-Script parsebar.

```json
{"t": 1234, "i": 1.23, "v": 150.5, "p": 412.0, "pwm": 45.2, "ss": 3, "ds": 1}
```

| Schlüssel | Parameter | Einheit |
|-----------|-----------|---------|
| `t` | Systemzeit [ms] | ms |
| `i` | `meas_current_a` | A |
| `v` | `meas_velocity_mms` | mm/s |
| `p` | `meas_position_mm` | mm |
| `pwm` | `meas_pwm_duty_pct` | % |
| `ss` | `meas_sys_state` | - |
| `ds` | `meas_door_state` | - |

Stream starten / stoppen:
```json
{"cmd": "stream", "en": true}
{"cmd": "stream", "en": false}
```

### 7.3  Framing

- Jede Nachricht: einzelne JSON-Zeile, abgeschlossen mit `\n`
- Baudrate: 115200 (konfigurierbar)
- Kein Binary-Framing nötig – JSON selbst-delimitierend durch `\n`
- HODOR ignoriert unbekannte `cmd`-Werte (graceful degradation)

---

## 8  Validierungs-Regeln

| Regel | Verhalten |
|-------|-----------|
| `val < min` | Abgelehnt; Fehlermeldung `"out_of_range"` |
| `val > max` | Abgelehnt; Fehlermeldung `"out_of_range"` |
| Unbekannte ID | Abgelehnt; Fehlermeldung `"unknown_id"` |
| Falscher Typ | Abgelehnt; Fehlermeldung `"type_mismatch"` |
| `READONLY`-Flag | Abgelehnt; Fehlermeldung `"readonly"` |
| Gültig | Wert übernommen; bei `PERSIST`-Flag optional sofort in NVS |

---

## 9  Erweiterungsregeln

- Neue Parameter immer am **Ende eines Blocks** anhängen, nie IDs umsortieren
- Gelöschte Parameter: ID reserviert lassen (als Kommentar markieren), nicht wiederverwenden
- Neue Blöcke ab nächster freier `0xXX00`-Grenze beginnen
- `name`-String muss projektweit eindeutig sein (wird als NVS-Schlüssel und JSON-Key genutzt)
- Max. NVS-Schlüssellänge: 15 Zeichen (ESP-IDF-Limit)
