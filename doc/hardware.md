# Hardware – HODOR

Pinbelegung, Schaltplan-Referenz und elektrische Grenzwerte.  
Architektur → `architecture.md` | Regler-Anbindung → `control_loop.md`

---

## 1  Mikrocontroller – ESP32-S3-WROOM-1

| Eigenschaft | Wert |
|-------------|------|
| Modul | ESP32-S3-WROOM-1 |
| Flash | 8 MB (Variante -N8) – mindestens erforderlich |
| PSRAM | optional (nicht genutzt in v1.0) |
| Takt | 240 MHz |
| Versorgung Modul | 3,3 V / max. 500 mA |

**Reservierte / nicht nutzbare GPIOs:**

| GPIO | Grund |
|------|-------|
| 26–32 | Intern mit Flash verbunden (WROOM-1) |
| 0 | Strapping-Pin (Boot-Modus) |
| 43, 44 | UART0 TX/RX (Flash/Debug) – freihalten |

---

## 2  Pinbelegung

> **Status: Platzhalter – Werte noch nicht festgelegt.**  
> Spalte „GPIO" ausfüllen sobald PCB-Layout beginnt. Danach in `hodor_config.h` als Konstanten eintragen.

### 2.1  H-Brücke / PWM

| Signal | GPIO | Richtung | Beschreibung |
|--------|------|----------|--------------|
| `MOT_PWM_1` | 47 | OUT | PWM Phase A (LEDC Kanal 0) |
| `MOT_PWM_2` | 48 | OUT | PWM Phase B (LEDC Kanal 1) |
| `MOT_EN` | 45 | OUT | H-Brücke Enable (aktiv High) |
| `MOT_NFAULT` | 46 | IN | H-Brücke Fault (aktiv Low, Pull-up) |

### 2.2  Treiber Konfiguration – MC33HB2002 (SPI)

| Signal | GPIO | Richtung | Beschreibung |
|--------|------|----------|--------------|
| `DRV_SCLK` | 12 | OUT | SPI Clock |
| `DRV_MISO` | 11 | IN | SPI MISO (Antwort) |
| `DRV_MOSI` | 13 | IN | SPI MOSI (Command) |
| `DRV_CS` | 10 | OUT | Chip Select (aktiv Low) |

### 2.3  Encoder (Quadratur) – PCNT

| Signal | GPIO | Richtung | Beschreibung |
|--------|------|----------|--------------|
| `ENC_A` | 35 | IN | Encoder Kanal A |
| `ENC_B` | 36 | IN | Encoder Kanal B |

> Alternativbelegung: `ENC_A` → Endschalter 1 offen/geschlossen; `ENC_B` → Endschalter 2. Konfigurierbar per NVS (`input_mode_X`).

### 2.4  Potentialfreie Steuereingänge

| Signal | GPIO | Richtung | Beschreibung |
|--------|------|----------|--------------|
| `IN_1` | 17 | IN | Eingang 1 (konfigurierbar) |
| `IN_2` | 18 | IN | Eingang 2 (konfigurierbar) |
| `IN_3` | 39 | IN | Eingang 3 (konfigurierbar) |

Eingangs-Modi (NVS `input_mode_X`):

| Wert | Modus | Verhalten |
|------|-------|-----------|
| 0 | Taster | Flanke → Event: Öffnen, Schließen, Wechseln|
| 1 | Schalter | Pegel → Zustand |
| 2 | Bewegungsmelder | Flanke High → Öffnen, Timeout → Schließen |
| 3 | Endschalter | Pegel → `EVT_POS_REACHED` |

### 2.5  Schaltbares Relais

| Signal | GPIO | Richtung | Beschreibung |
|--------|------|----------|--------------|
| `OUT_1` | 3 | OUT | MOSFET Gate; schaltet Relais |

### 2.6  Hardware-Watchdog – 74LVC1G123

| Signal | GPIO | Richtung | Beschreibung |
|--------|------|----------|--------------|
| `WDG_TRIG` | 42 | OUT | Retriggerpuls; Periode < Watchdog-Timeout |
| `WDG_NCLR` | 41 | OUT | Watchdog-Reset (sofort Aus, low-aktiv) |

### 2.7  Sonstiges

| Signal | GPIO | Richtung | Beschreibung |
|--------|------|----------|--------------|
| `LED_STATUS` | 16 | OUT | Status-LED State |
| `LED_ACTIVE` | 15 | OUT | Status-LED Aktivität  |
| `LED_CONN` | 14 | OUT | Status-LED Wifi |
| `BOOT` | 0 | IN | Strapping; 10 kΩ Pull-up; Taster nach GND |
| `UART_TX` | 43 | OUT | Debug UART |
| `UART_RX` | 44 | IN | Debug UART |

---

## 3  Elektrische Grenzwerte

### 3.1  Versorgung

| Schiene | Nennspannung | Bereich | Max. Strom | Quelle |
|---------|-------------|---------|------------|--------|
| `VCC_MOT` | 12–24 V | 10–26 V | 5 A (Motor) | Extern |
| `VCC_5V ` | 5,0 V | 4,8–5,2 V | 1000 mA | DC/DC aus `VCC_MOT` |
| `VCC_3V3` | 3,3 V | 3,0–3,6 V | 500 mA | DC/DC aus `VCC_5V` |

### 3.2  GPIO-Grenzwerte ESP32-S3

| Parameter | Wert |
|-----------|------|
| Eingangsspannung max. | 3,6 V |
| Ausgangsstrom max. pro Pin | 40 mA |
| Gesamtstrom alle GPIOs | 1080 mA (nicht dauerhaft) |
| Eingangsschwelle High | > 0,75 × VDD |
| Eingangsschwelle Low | < 0,25 × VDD |

> **Achtung:** Optokoppler-Ausgänge und H-Brücken-Fault-Signale auf 3,3-V-Pegel pegelwandeln oder mit Spannungsteiler begrenzen.

### 3.3  ACS725 – Strommessung

| Parameter | Wert |
|-----------|------|
| Messbereich | ±5 A (ACS725LLCTR-05AB) |
| Empfindlichkeit | 400 mV/A |
| Ausgangsspannung bei 0 A | ~1,5 V (VCC/2 bei 3,3 V) |
| Versorgung | 3,3 V |
| Bandbreite | 80 kHz |

Ausgangsspannungsbereich: 1,5 V ± (5 A × 0,4 V/A) = **0,5 V … 2,5 V**.


### 3.4  74LVC1G123 – Hardware-Watchdog

| Parameter | Wert |
|-----------|------|
| Versorgung | 3,3 V |
| Timeout-Zeit | 200 ms (extern R/C) |
| Retriggerpuls-Breite | min. 10 ns |
| Ausgangs-Logikpegel | 3,3 V (kompatibel mit ESP32-S3) |

Timeout-Berechnung: `t = 0,7 × R × C` – Werte nach PCB-Layout eintragen.

---

## 4  Schaltplan-Referenz

### 4.1  Stromversorgung

```
VCC_MOT (12–24 V)
    │
    ├── Verpolschutz ──► LM74610 + N-MOSFET (Bootstrap Prevention)
    │
    ├── DC/DC-Wandler ──────► VCC_5V (Encoder, Eingänge)
    │
    └── DC/DC-Wandler ───────────────► VCC_3V3 (ESP32-S3, Logik)
```

> Verpolschutz / Bootstrap-Prevention bei manuellem Türbewegen: LM74610-Lösung, siehe Designentscheidungen in `CLAUDE.md`.

### 4.2  H-Brücke

```
ESP32-S3
  MOT_PWM_A ──► H-Brücken-IC IN_A
  MOT_PWM_B ──► H-Brücken-IC IN_B
  MOT_EN    ──► H-Brücken-IC EN
  MOT_NFAULT ◄── H-Brücken-IC FAULT (Open-Drain + Pull-up 10 kΩ)

H-Brücken-IC
  OUT_A / OUT_B ──► Motor (12–24 V / max. 5 A)
  VCC ──────────── VCC_MOT
  GND ──────────── GND (Leistungserde)
```

### 4.3  Watchdog – 74LVC1G123

```
ESP32-S3
  WDG_TRIG ──► 74LVC1G123 A (Trigger-Eingang)

74LVC1G123
  Rext / Cext ──► Timeout-Zeitkonstante
  /Q ──────────► H-Brücken-IC EN (aktiv Low → Motor aus bei Timeout)
  Q  ──────────► WDG_OUT (optional Rückmeldung an ESP32-S3)
```

### 4.5  Potentialfreie Eingänge

```
Externer Kontakt (potentialfrei)
    │
    Signal ──► Pull-up 10 kΩ ── 3,3 V
               │
               IN_x (GPIO ESP32-S3)
```

---

## 5  Noch offen / TBD

| Punkt | Status |
|-------|--------|
| Alle GPIO-Nummern | Platzhalter – nach PCB-Layout eintragen |
| PWM-Deadtime H-Brücke | Abhängig von gewähltem H-Brücken-IC |
| H-Brücken-IC Typ | MC33HB2002 |
| BOM | Separates Dokument |
