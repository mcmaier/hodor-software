# Funktionsbeschreibung: DC-Motorsteuerung für Schiebetürantriebe

**Quelle:** Hyperion-i Firmware v0.43.0 (Device ID: HYP_I04)
**Zielplattform (Original):** Atmel AT91SAM7S256, ARM Cortex-M3
**Zweck:** Extraktion der Kernfunktionalität zur Übertragung auf ein neues System

---

## 1. Systemübersicht

Das System steuert DC-Kleinmotoren für lineare Bewegungsaufgaben (Schiebetüren, Torantriebe) mit definiertem Anfangs- und Endpunkt. Es realisiert:

- Trapezoidal-Fahrprofile mit Rampensteuerung (Beschleunigung, Konstantfahrt, Verzögerung)
- Kaskadenregelung: Position → Drehzahl → Strom → PWM
- Mehrfachige Strombegrenzung zum Personen- und Antriebsschutz
- Automatische Einlernfahrt (Wegmessung, Strommessung, Encodererkennung)
- Ereignisgesteuerte Zustandsmaschine mit 12 Hauptzuständen

**Steuerzykluszeit:** 1 ms (1 kHz Regelschleife)

---

## 2. Regelungsarchitektur

### 2.1 Kaskadenregelung (Übersicht)

```
Sollposition [mm]
      ↓
┌─────────────────┐
│ Positionsregler │  → Sollgeschwindigkeit [mm/s]
└─────────────────┘
      ↓
┌─────────────────┐
│ Drehzahlregler  │  PI + PT1-Filter + Feedforward  → Sollstrom [mA]
└─────────────────┘
      ↓
┌─────────────────┐
│  Stromregler    │  PI + Gegen-EMK-Kompensation    → Soll-PWM [%]
└─────────────────┘
      ↓
┌─────────────────┐
│  Strombegrenzer │  Harte Strombegrenzung (Schutzschicht)
└─────────────────┘
      ↓
    PWM → H-Brücke → Motor
```

### 2.2 Steuermodi

| Modus            | Beschreibung                                            |
|------------------|---------------------------------------------------------|
| `PWM`            | Direkte PWM-Vorgabe (0–100 %)                           |
| `VOLTAGE`        | Spannungssteuerung mit Rampenbegrenzung                 |
| `CURRENT`        | Stromregelung (Drehmomentsteuerung)                     |
| `VELOCITY`       | Drehzahlregelung (Kaskade: Drehzahl → Strom → PWM)     |
| `VELOCITY_LIMIT` | Drehzahl mit Strombegrenzung                            |
| `POSITION`       | Positionsregelung (Kaskade: Position → Drehzahl → Strom)|

---

## 3. Fahrprofil (Rampengenerator)

### 3.1 Bewegungsphasen

Das Fahrprofil teilt eine Fahrt in drei Phasen:

```
Geschwindigkeit
    ^
    |        ___________
    |       /           \
    |      /             \
    |_____/_______________\____→ Weg
         Accel  Konst.  Decel
```

| Phase               | Beschreibung                                         |
|---------------------|------------------------------------------------------|
| `MOVEMENT_START`    | Beschleunigung bis Nenndrehzahl                      |
| `MOVEMENT_MIDDLE`   | Konstantfahrt mit Nenngeschwindigkeit                |
| `MOVEMENT_END`      | Verzögerung bis Stillstand (oder Kriechgeschwindigkeit) |

### 3.2 Fahrprofilberechnung

**Beschleunigungsphase** (lineare Rampe):
```
v_acc(t) = a_acc × t
```

**Verzögerungsphase** (kinematisch, aus verbleibender Wegstrecke):
```
v_dec(x) = sqrt(v_slow² + 2 × a_dec × (x_target − x))
```

**Aktuelle Sollgeschwindigkeit** = Minimum aus v_acc und v_dec
→ Sicherstellt weichen Übergang zwischen den Phasen.

### 3.3 Fahrprofilparameter

| Parameter                    | Typ      | Einheit  | Standard | Beschreibung                              |
|------------------------------|----------|----------|----------|-------------------------------------------|
| `acceleration_mmps2`         | uint16   | mm/s²    | 250      | Beschleunigung                            |
| `deceleration_mmps2`         | uint16   | mm/s²    | 100      | Verzögerung                               |
| `switchToSpeedControl_mm`    | uint16   | mm       | 5        | Umschaltpunkt auf Kriechfahrt vor Endposition |
| `nominalSpeed_cmps`          | —        | cm/s     | —        | Nenngeschwindigkeit Vollöffnung           |
| `reducedSpeed_cmps`          | —        | cm/s     | —        | Reduzierte Geschwindigkeit                |
| `slowSpeed_mmps`             | —        | mm/s     | —        | Kriechgeschwindigkeit (Endanfahrt)        |
| `overrideSpeedRamp`          | Bool     | —        | false    | Rampe deaktivieren (Sofortstart)          |

---

## 4. Drehzahlregler (PI + Feedforward)

### 4.1 Reglerstruktur

```
Sollwert [mm/s] → PT1-Filter → ┐
                                ├→ PI-Regler → Ausgang [mA]
Istwert [mm/s]  ────────────→  ┘
                                    ↑
                            Feedforward (Drehzahl × kv)
```

### 4.2 Parameter

| Parameter           | Typ    | Wertebereich | Standard        | Beschreibung                  |
|---------------------|--------|-------------|-----------------|-------------------------------|
| `kpn_div_100`       | uint16 | —           | 1200 → Kp = 12  | Proportionalanteil            |
| `kin_div_100`       | uint16 | —           | 2100 → Ki = 21  | Integralanteil                |
| `kvn_div_100`       | uint16 | —           | 150 → Kv = 1,5  | Feedforward-Faktor            |
| `t_filter_ms`       | uint16 | ms          | 150             | PT1-Zeitkonstante Sollwertfilter |
| `minimumOutput_mA`  | int16  | mA          | 50              | Mindestausgabe (Totzone)      |
| `maximumOutput_mA`  | int16  | mA          | 3000            | Maximalausgabe                |
| `fastStopDelay`     | uint8  | —           | —               | Bremsverzögerungsfaktor       |

**Anti-Windup:** Integratorbegrenzung ±500.000; Einfrieren bei gesättigtem Ausgang.

---

## 5. Stromregler (PI + Gegen-EMK-Kompensation)

### 5.1 Reglerstruktur

```
Sollstrom [mA] → ┐
                  ├→ PI-Regler → PWM-Ausgabe [%]
Iststrom [mA]  → ┘
                      ↑
            Feedforward: u_ff = n × k_ff  (Gegen-EMK)
```

Gegen-EMK-Berechnung:
```
u_back_emf ≈ rotationSpeed_cHz / 100   [V]
```

### 5.2 Parameter

| Parameter                | Typ    | Wertebereich | Standard            | Beschreibung                  |
|--------------------------|--------|-------------|---------------------|-------------------------------|
| `kpi_div_10000`          | uint16 | —           | 450 → Kp = 0,045    | Proportionalanteil            |
| `kii_div_10000`          | uint16 | —           | 5500 → Ki = 0,55    | Integralanteil                |
| `k_ffwd_ctrl_div_100`    | uint8  | —           | 30 → Kff = 0,3      | Feedforward-Faktor            |
| `feedforwardEnable`      | Bool   | —           | —                   | Gegen-EMK-Kompensation aktiv  |
| `minimumOutput_pwmPercent` | int8 | %           | 5                   | Mindest-PWM                   |
| `maximumOutput_pwmPercent` | int8 | %           | 100                 | Maximal-PWM                   |

**Anti-Windup:** Integratorbegrenzung ±5.000.000.

---

## 6. Strombegrenzung und Personenschutz

### 6.1 Mehrstufiger Schutz

Das System realisiert vier unabhängige Schutzmechanismen:

#### Stufe 1 – Harte Strombegrenzung (Hardware-nahe Schutzschicht)
- Absolute Stromgrenze aus der Einlernfahrt
- PWM wird sofort reduziert, wenn `I_ist > I_limit − 100 mA`
- PI-Regler mit reduzierten Verstärkungen
- Standard-Limit: **2500 mA**

#### Stufe 2 – Zeitbasierte Überstromdetektion
- Auslösung, wenn Strom für `overcurrentDetectionTime_ms` überschritten
- Verhindert thermische Überlastung bei anhaltender Blockade

#### Stufe 3 – Wegbasierte Überstromdetektion
- Auslösung, wenn Überstrom über mehr als `overcurrentDetectionWay_mm` anhält
- Erkennt räumlich ausgedehnte Hindernisse unabhängig von der Zeit

#### Stufe 4 – Dynamische Stromdifferenzierung
- Überwacht die zeitliche Änderungsrate des Stroms (dI/dt)
- Erkennt plötzliche Stromspitzen bei Hinderniskontakt noch vor statischem Limit
- Aktivierbar per `currentDifferentiationEnabled`

### 6.2 Phasenabhängige Schutzkennlinien

13-Punkt-Lookup-Table mit separaten Zeit- und Stromgrenzen je Fahrtabschnitt:

| Fahrtphase            | Eigene Stromgrenze | Eigenes Zeitlimit |
|-----------------------|--------------------|--------------------|
| Öffnen – Startphase   | ✓                  | ✓                  |
| Öffnen – Mitte        | ✓                  | ✓                  |
| Öffnen – Endphase     | ✓                  | ✓                  |
| Schließen – Startphase| ✓                  | ✓                  |
| Schließen – Mitte     | ✓                  | ✓                  |
| Schließen – Endphase  | ✓                  | ✓                  |
| … (bis 13 Punkte)     |                    |                    |

### 6.3 Schutzparameter

| Parameter                          | Typ    | Einheit | Beschreibung                             |
|------------------------------------|--------|---------|------------------------------------------|
| `allowedMaximumCurrent_mA`         | uint16 | mA      | Absolute Stromgrenze (aus Einlernfahrt)  |
| `overcurrentDetectionTime_ms`      | —      | ms      | Zeitbasis Überstromdetektion             |
| `overcurrentDetectionWay_mm`       | —      | mm      | Wegbasis Überstromdetektion              |
| `overcurrentDetectionLevel_percent`| —      | %       | Schwelle als % des Nennstroms            |
| `currentDifferentiationEnabled`    | Bool   | —       | dI/dt-Überwachung aktiv                  |
| `currentDifferentiationLimit`      | —      | —       | dI/dt-Schwellwert                        |
| `currentToleranceAtNominalSpeed`   | uint8  | —       | Stromtoleranzband bei Nenngeschwindigkeit|
| `currentToleranceAtReducedSpeed`   | uint8  | —       | Stromtoleranzband bei Kriechfahrt        |
| `disableObstructionDetection`      | Bool   | —       | Hinderniserkennung deaktivieren          |

---

## 7. Positionsmessung und Encoder

### 7.1 Encodertypen

| Typ              | Beschreibung                                      |
|------------------|---------------------------------------------------|
| `ENCODER_MOTOR`  | Encoder am Motor (vor Getriebe)                   |
| `ENCODER_LOAD`   | Encoder an der Last/Tür (nach Getriebe)           |

### 7.2 Quadraturdekodierung

Zustandsmaschine mit 4 Zuständen (A0B0, A1B0, A0B1, A1B1) für Vor-/Rückwärtszählung. Stromabfall ohne Bewegung löst Stopperkennung aus.

### 7.3 Positionsumrechnung

```
position_mm = increments × mmPerIncrement
mmPerIncrement = way_mm (gelernt) / increments_total (gelernt)
```

### 7.4 Encoderparameter

| Parameter                  | Typ    | Einheit    | Beschreibung                        |
|----------------------------|--------|-----------|-------------------------------------|
| `encoderResolution`        | uint32 | Inc/Umdrehung | Encoderauflösung                |
| `reverseEncoderDirection`  | Bool   | —         | Zählrichtung invertieren            |
| `encoderEnforcement`       | Bool   | —         | Encoder erzwingen (auch ohne Signal)|
| `currentPosition_inc`      | int32  | Inkrement | Aktuelle Position (absolut)         |

### 7.5 Stopperkennung ohne Encoder

Alternativbetrieb (Spannungssteuerung ohne Encoder):
- Zeitbasierter Stopp: `automaticShutdownTime_ms` nach Fahrtbeginn
- Strombasierter Stopp: Blockadestrom über Schwelle
- Adaptive Kalibrierung der Verzögerungszeit aus gemessenen Zykluszeiten

---

## 8. Automatische Einlernfahrt (Autoconfig)

### 8.1 Ablauf

```
1. Encodererkennung          → Encoder vorhanden?
2. Erste Messfahrt           → Türweg messen [mm], Encoder-Inkremente zählen
3. Zweite Messfahrt          → Verifikation
4. (ohne Encoder) Motorcharakterisierung
5. Berechnung aller Systemparameter
```

### 8.2 Ergebnisse der Einlernfahrt

**Systemkalibrierung (SystemCalibration_t):**

| Feld                         | Typ    | Einheit | Beschreibung                           |
|------------------------------|--------|---------|----------------------------------------|
| `calibrationValid`           | Bool   | —       | Einlernfahrt abgeschlossen             |
| `way_mm`                     | int16  | mm      | Gemessener Türweg                      |
| `increments`                 | int32  | Inc     | Encoder-Inkremente für Gesamtweg       |
| `automaticShutdownTime_ms`   | —      | ms      | Timeout für automatischen Stopp        |
| `detectedMaxSpeed`           | uint16 | mm/s    | Maximal erreichbare Geschwindigkeit    |

**Stromkalibrierung (CurrentCalibration_t):**

| Feld                                    | Einheit | Beschreibung                              |
|-----------------------------------------|---------|-------------------------------------------|
| `allowedMaximumCurrent_mA`              | mA      | Zulässiger Maximalstrom                   |
| `currentAtNominalSpeed_mA`              | mA      | Nennstrom bei Vollgeschwindigkeit         |
| `currentAtReducedSpeed_mA`             | mA      | Nennstrom bei Kriechgeschwindigkeit       |
| `cycleDurationAtNominalSpeed_centisec`  | cs      | Gemessene Zykluszeit bei Vollgeschwindigkeit |
| `cycleDurationAtReducedSpeed_centisec`  | cs      | Gemessene Zykluszeit bei Kriechgeschwindigkeit |

---

## 9. Hauptzustandsmaschine

### 9.1 Zustände

| Zustand             | Beschreibung                                        |
|---------------------|-----------------------------------------------------|
| `STATE_start`       | Hochlauf, Hardware-Initialisierung                  |
| `STATE_standby`     | Betriebsbereit, Motor inaktiv                       |
| `STATE_preActive`   | Vorbereitung (mechanische Vorspannung, Lösen)       |
| `STATE_active`      | Motor läuft, Tür in Bewegung                        |
| `STATE_postActive`  | Nachbehandlung nach Fahrtende                       |
| `STATE_configuration` | Manuelle Parameterkonfiguration                   |
| `STATE_autoconfig`  | Automatische Einlernfahrt                           |
| `STATE_extendedConfig` | Erweiterte Konfigurationsoptionen               |
| `STATE_timeConfig`  | Zeitparameter-Konfiguration                         |
| `STATE_selftest`    | Selbsttest-Modus                                    |
| `STATE_locked`      | System gesperrt                                     |
| `STATE_error`       | Fehlerzustand                                       |

### 9.2 Türzustände

| Zustand              | Beschreibung              |
|----------------------|---------------------------|
| `DOORSTATE_closed`   | Tür vollständig geschlossen|
| `DOORSTATE_opening`  | Tür öffnet                |
| `DOORSTATE_open`     | Tür vollständig geöffnet  |
| `DOORSTATE_closing`  | Tür schließt              |

### 9.3 Stoppursachen

| Code                  | Beschreibung                          |
|-----------------------|---------------------------------------|
| `STOP_END_REACHED`    | Endposition erreicht (normal)         |
| `STOP_USER_REQUEST`   | Manueller Stopp                       |
| `STOP_REVERSE_REQUEST`| Hindernis erkannt, Umkehr eingeleitet |
| `STOP_OVERCURRENT`    | Stromgrenze überschritten             |
| `STOP_NO_MOTION`      | Keine Bewegung erkannt (Encoder)      |
| `STOP_TIMEOUT`        | Zeitlimit überschritten               |
| `STOP_DRIVERFAULT`    | H-Brücken-Fehlersignal               |
| `STOP_NO_FEEDBACK`    | Encoder-/Sensorfehler                 |

---

## 10. Betriebsmodi

| Modus            | Beschreibung                                                    |
|------------------|-----------------------------------------------------------------|
| `MANUAL`         | Steuerung nur über direkte Eingangssignale                      |
| `SEMIAUTOMATIC`  | Öffnen auf Befehl, automatisches Schließen nach Zeitablauf      |
| `AUTOMATIC`      | Vollautomatisch mit Präsenzdetektion                            |

**Erweiterte Modi:**

| Modus             | Beschreibung                            |
|-------------------|-----------------------------------------|
| `PARTIAL_OPEN`    | Teilöffnung auf konfigurierten Weg      |
| `MIDDLE_OPEN`     | Mittlere Öffnungsposition               |
| `AIRLOCK`         | Schleusenbetrieb (koordinierte Türen)   |

---

## 11. Ein-/Ausgänge

### 11.1 Digitale Eingänge

| Signal       | Funktion                                        |
|--------------|-------------------------------------------------|
| AUF          | Öffnen (Taster oder Dauerbefehl)                |
| ZU           | Schließen                                       |
| STOP         | Nothalt                                         |
| RESET        | System-Reset                                    |
| IN1, IN2     | Konfigurierbare Eingänge                        |
| FUNK_AUF/ZU  | Funksignal Öffnen/Schließen                     |

**Konfigurierbare Eingangsfunktionen:** Endschalter, Teilöffnung, Verriegelung, Freigabe, Umkehr, Sperren, Override, Rückmeldung.

### 11.2 Ausgänge

| Signal       | Funktion                                        |
|--------------|-------------------------------------------------|
| OUT1         | Relaisausgang (konfigurierbar)                  |
| POUT1, POUT2 | Proportionalausgang 0–10 V (PWM-basiert)        |

**Ausgangsfunktionen:** Statusmeldungen (öffnet, schließt, Fehler, gesperrt, …).

---

## 12. Messgrößen und Systemzustand

### 12.1 Echtzeitwerte (je 1 ms aktualisiert)

| Variable               | Einheit       | Beschreibung                    |
|------------------------|---------------|---------------------------------|
| `pwmDutyCycle_percent` | %  (±100)     | Aktueller PWM-Tastgrad          |
| `current_mA`           | mA            | Motorstrom                      |
| `rotationSpeed_cHz`    | 0,01 U/min    | Motordrehzahl                   |
| `linearSpeed_mmps`     | mm/s          | Lineare Türgeschwindigkeit      |
| `mainVoltage_mV`       | mV            | Versorgungsspannung             |
| `currentPosition_mm`   | mm            | Türposition                     |

### 12.2 Spannungsüberwachung

| Parameter               | Beschreibung                          |
|-------------------------|---------------------------------------|
| `voltageCriticalLimit_mV` | Kritischer Spannungsgrenzwert       |
| `voltageLowerLimit_mV`  | Unterspannungsgrenze                  |
| `voltageUpperLimit_mV`  | Überspannungsgrenze                   |
| `voltageLimitationTime_ms` | Hystereseverzögerung              |

---

## 13. Systemparameter (Hardware-Konfiguration)

| Parameter                  | Einheit      | Beschreibung                              |
|----------------------------|--------------|-------------------------------------------|
| `switchingFrequency_Hz`    | Hz           | PWM-Trägerfrequenz (typisch 16–20 kHz)    |
| `gearRatio_div_10`         | —            | Getriebeübersetzung × 10                  |
| `driveBeltPulleyRadius_100um` | 100 µm    | Riemenscheibenradius für mm/s-Berechnung  |
| `baudrate`                 | Baud         | Serielle Kommunikation (Standard: 115200) |
| `delayBeforeReset_ms`      | ms           | Watchdog-Reset-Verzögerung (Standard: 2000)|

---

## 14. Datenpersistenz

- **24 Parameterstrukturen**, davon **21 im EEPROM gespeichert**
- Alle Strukturen mit **CRC-Prüfsumme** gesichert
- Parameterübertragung per serieller Schnittstelle (Upload/Download)
- Bootloader-Unterstützung über Firmware-Partition-Management

---

## 15. Sicherheitsfunktionen (Zusammenfassung)

| Funktion                        | Beschreibung                                              |
|---------------------------------|-----------------------------------------------------------|
| Watchdog                        | Hardware-Reset bei ausgefallener Regelschleife            |
| CRC-Schutz                      | Alle persistenten Daten checksummengesichert              |
| Anti-Windup                     | Verhindert Integratorsättigung in allen PI-Reglern        |
| 4-stufiger Stromschutz          | Hart, zeitbasiert, wegbasiert, dynamisch                  |
| Phasenabhängige Schutzkennlinie | 13-Punkt-Tabelle je Fahrtrichtung und -phase              |
| Spannungsüberwachung            | Unter-/Überspannung mit Hysterese                         |
| Encoderüberwachung              | Stopperkennung bei fehlendem Bewegungssignal              |
| Blockadeerkennung + Umkehr      | Automatisches Reversieren bei Hinderniskontakt            |
| Umkehrlimitzähler               | Maximale Anzahl aufeinanderfolgender Reversierungen        |
| Fahrtzeitlimit                  | Timeout-basierter Notstopp                                |
| Sanftes Abbremsen               | Keine abrupten Motorstopps (Bremsstopp-Rampe)             |
| Treiberfehlerüberwachung        | Auswertung des H-Brücken-Fault-Signals                    |

---

## 16. Hinweise zur Portierung

### Kritische Schnittstellen

1. **ADC** – Strom und Spannung: 8-fach gemittelt, ~16 kHz Abtastrate, auf 1 kHz dezimiert; Bereichsumschaltung für Hoch-/Niedrigstrom
2. **PWM** – Motoransteuerung: Trägerfrequenz 16–20 kHz; FET-Enable + INHIBIT-Signal getrennt steuerbar
3. **Encoder-Eingang** – Quadratur, 2 Kanäle; Zustandsmaschine für Dekodierung
4. **EEPROM** – I²C (TWI); CRC-gesicherte Parameterblöcke
5. **Seriell** – USART 115200 Baud; Protokoll für Parametrisierung

### Regelparameter-Empfehlungen

- Alle PI-Regler enthalten Anti-Windup – dieses **muss** in der Portierung erhalten bleiben
- Feedforward im Stromregler (Gegen-EMK) ist essenziell für gute Regelgüte
- PT1-Filter im Drehzahlregler verhindert Schwingen bei sprungförmiger Sollwertvorgabe
- Normierung: Kp/100, Ki/100 (Drehzahl); Kp/10000, Ki/10000 (Strom) → bei Portierung auf Gleitkomma umrechnen

### Muss-Funktionen für Neuimplementierung

1. 1 ms Regelschleife (Timer-Interrupt oder RTOS-Task)
2. Kaskadenregelung Position → Drehzahl → Strom → PWM
3. Trapez-Fahrprofil mit konfigurierbaren Rampen
4. Harte Strombegrenzung (unterste Schutzschicht)
5. Zeitbasierte + dynamische Überstromdetektion
6. Quadratur-Encoderdekodierung mit Positionsumrechnung
7. Automatische Einlernfahrt (Wegmessung + Strommessung)
8. Ereignisgesteuerte Zustandsmaschine (min. Standby / Aktiv / Fehler)
9. CRC-gesicherte Parameterstruktur im nichtflüchtigen Speicher
10. Watchdog-Aktivierung
