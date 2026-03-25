# Regelkreis – HODOR

Kaskadenregelung mit drei verschachtelten PI-Reglern und optionaler Ruckbegrenzung.  
Übergeordnete Architektur → `architecture.md` | Zustandsbedingungen → `state_machine.md`

---

## 1  Übersicht

```
Sollposition [mm]
      │
      ▼  10 ms
┌─────────────┐
│ Positionsregler │  (P)
└──────┬──────┘
       │ Sollgeschwindigkeit [mm/s]
       │
       ▼  optional
┌─────────────────┐
│ Ruckbegrenzer   │  S-Kurven-Profil
└──────┬──────────┘
       │ Sollgeschwindigkeit (begrenzt)
       │
       ▼  1 ms
┌─────────────┐
│ Geschwindigkeitsregler │  (PI)
└──────┬──────┘
       │ Sollstrom [A]
       │
       ▼  100 µs
┌─────────────┐
│ Stromregler │  (PI)
└──────┬──────┘
       │ PWM-Duty [%]
       │
       ▼  20 kHz
   H-Brücke → Motor
```

---

## 2  Stromregler (innerster Loop)

| Parameter | Wert |
|-----------|------|
| Abtastzeit | 100 µs |
| Typ | PI |
| Istwert-Quelle | ADC; Sample-Trigger PWM-synchron (Mitte Low-Side) |
| Istwert-Einheit | A (bidirektional; negativ = Gegenstrom) |
| Ausgabe | PWM-Duty [-100 % … +100 %]; Vorzeichen → H-Brücken-Richtung |
| Anti-Windup | Integrator-Clamping auf `±motor_max_a` |
| Kp | `TBD` |
| Ki | `TBD` |

**Besonderheiten:**
- Sample-Zeitpunkt: Mitte des PWM-Low-Side-Intervalls → minimales Schaltripple
- ACS725 Midpoint ~1,5 V → maximale ADC-Dynamik ohne Sättigung
- ISR-Handler: `IRAM_ATTR`; kein FreeRTOS-API-Aufruf; Ergebnis via Queue an `control_task`

---

## 3  Geschwindigkeitsregler (mittlerer Loop)

| Parameter | Wert |
|-----------|------|
| Abtastzeit | 1 ms |
| Typ | PI |
| Istwert-Quelle | Encoder (PCNT-Delta / ms) oder Δ-Position aus Endschalter-Logik |
| Istwert-Einheit | mm/s |
| Ausgabe | Sollstrom [A]; begrenzt auf `±motor_max_a` |
| Anti-Windup | Integrator-Clamping |
| Kp | `TBD` |
| Ki | `TBD` |

**Besonderheiten:**
- Ohne Encoder: Geschwindigkeit aus Positions-Differenz (10-ms-Wert interpoliert auf 1 ms)
- Integrator einfrieren wenn `|v_ist| < v_min_thresh` und Motor stromlos (Stillstand-Erkennung)

---

## 4  Positionsregler (äußerer Loop)

| Parameter | Wert |
|-----------|------|
| Abtastzeit | 10 ms |
| Typ | P (reiner Proportionalregler) |
| Istwert-Quelle | Encoder-Zähler [mm] oder Endschalter (binär) |
| Istwert-Einheit | mm |
| Ausgabe | Sollgeschwindigkeit [mm/s]; begrenzt auf `±v_max` |
| Kp | `TBD` |

**Besonderheiten:**
- Reiner P-Regler ausreichend, da unterlagerte Loops den stationären Fehler ausregeln
- Endschalter-Betrieb: Position wird bei Endschalter-Auslösung hart auf 0 bzw. `door_open_mm` gesetzt (Kalibrierung)
- Positionsfenster `pos_tolerance_mm`: Innerhalb → `EVT_POS_REACHED` an Tür-SM

---

## 5  Ruckbegrenzer (optional)

Aktivierbar per `cfg_jerk_limit_enable` in NVS. Sitzt zwischen Positionsregler-Ausgabe und Geschwindigkeitsregler-Eingang.

### Prinzip: S-Kurven-Profil

```
v [mm/s]
  v_max ─────────────┐         ┌──────────
                    /│         │\
                   / │         │ \
                  /  │         │  \
─────────────────/   │         │   \──────── t
        Anfahren     Konstantfahrt  Abbremsen
        (Jerk ↑)                   (Jerk ↓)
```

| Parameter | NVS-Schlüssel | Einheit | Default |
|-----------|--------------|---------|---------|
| Max. Ruck | `ramp_jerk` | mm/s³ | `TBD` |
| Max. Beschleunigung | `ramp_accel_max` | mm/s² | `TBD` |
| Max. Geschwindigkeit | `v_max` | mm/s | `TBD` |

**Implementierung:**
- Zustandsvariablen: `v_ref`, `a_ref` (werden jeden 10-ms-Tick integriert)
- Begrenzung: `|Δa| ≤ jerk · dt` pro Schritt
- Deaktiviert (`cfg_jerk_limit_enable = false`): Ausgabe = Positionsregler-Ausgabe direkt (trapezförmiges Profil)

---

## 6  Anti-Windup & Sicherheitsgrenzen

| Grenze | NVS-Schlüssel | Beschreibung |
|--------|--------------|--------------|
| Max. Strom | `motor_max_a` | Integrator-Clamp Strom- und Geschwindigkeitsregler |
| Max. Geschwindigkeit | `v_max` | Clamp Positionsregler-Ausgabe |
| Überstrom-Timeout | `overcurrent_ms` | Dauer bis `EVT_BLOCKED` (→ Tür-SM) |
| Stillstand-Schwelle | `v_min_thresh` | Unterhalb: Integrator einfrieren |
| Positions-Toleranz | `pos_tolerance_mm` | Fenster für `EVT_POS_REACHED` |

---

## 7  Task-Struktur (Core 1)

```
control_task  (Core 1, Prio: hoch)
    │
    ├── alle 100 µs:  Stromregler  ◄── ISR liefert ADC-Wert via Queue
    ├── alle   1 ms:  Geschwindigkeitsregler
    ├── alle  10 ms:  Positionsregler + Ruckbegrenzer
    └── alle  10 ms:  Überstrom-Überwachung → ggf. EVT_BLOCKED
```

- Ticker-Basis: `esp_timer` (High-Resolution Timer) oder LEDC-ISR-Trigger
- Regler-Koeffizienten zur Laufzeit per `cfg_get()` lesbar (kein Neustart nötig)
- Regler-Zustand (`integrator`, `v_ref`, `a_ref`) wird bei `ctrl_disable()` zurückgesetzt

---

## 8  Inbetriebnahme-Reihenfolge

1. Stromregler parametrieren (Sprungantwort auf kleinen Sollstrom)
2. Geschwindigkeitsregler parametrieren (Stromregler als Strecke)
3. Positionsregler parametrieren (beide unteren Loops als Strecke)
4. Ruckbegrenzer aktivieren und `ramp_jerk` / `ramp_accel_max` einstellen
5. `motor_max_a`, `overcurrent_ms` und `pos_tolerance_mm` validieren
