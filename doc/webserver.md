# Webserver – HODOR

Spezifikation für Captive Portal, Konfigurations- und Statusseiten.  
Implementierung: ESP-IDF `esp_http_server` in `comm/webserver.c`  
Parameter-Zugriff ausschließlich über `param_get()` / `param_set()` → `docs/parameters.md`

---

## 1  Betriebsmodi

| Modus | WLAN-Rolle | IP | Aktivierung |
|-------|-----------|-----|-------------|
| **AP-Modus** (Erstinbetriebnahme) | Access Point | 192.168.4.1 | Kein gespeichertes WLAN vorhanden |
| **STA-Modus** (Normalbetrieb) | Station | DHCP oder statisch | Nach erfolgreichem WLAN-Connect |

Übergang AP → STA: Nach Speichern gültiger WLAN-Credentials → Neustart.  
Übergang STA → AP: WLAN-Verbindung nach `wifi_retry_max` Versuchen fehlgeschlagen → AP-Modus.

---

## 2  Authentifizierung

| Parameter | NVS-Schlüssel | Default |
|-----------|--------------|---------|
| Auth aktiviert | `web_auth_en` | `false` |
| Benutzername | `web_user` | `"admin"` |
| Passwort | `web_pass` | `"hodor"` |

- Methode: HTTP Basic Auth (`WWW-Authenticate: Basic realm="HODOR"`)
- Gilt für alle Seiten außer Captive-Portal-Erkennung (`/generate_204`, `/hotspot-detect.html`, `/ncsi.txt`)
- Im AP-Modus (Erstinbetriebnahme): Auth **immer deaktiviert**

---

## 3  Captive Portal (AP-Modus)

Zweck: Browser leitet nach WLAN-Verbindung automatisch auf Konfigurationsseite weiter.

### 3.1  Erkennungs-Endpunkte

| Endpunkt | Antwort | Für |
|----------|---------|-----|
| `GET /generate_204` | `302 → http://192.168.4.1/` | Android |
| `GET /hotspot-detect.html` | `302 → http://192.168.4.1/` | iOS / macOS |
| `GET /ncsi.txt` | `302 → http://192.168.4.1/` | Windows |
| `GET /` | WLAN-Setup-Seite | Alle |

### 3.2  Seite: WLAN-Setup (`/`)

Felder:

| Feld | Typ | Beschreibung |
|------|-----|--------------|
| SSID | Text (dropdown + manuell) | Verfügbare Netzwerke; Scan-Button |
| Passwort | Password | WPA2-Passwort |
| Gerätename | Text | mDNS-Hostname (`hodor.local`) |

Aktionen:
- **Verbinden** → speichert Credentials in NVS, Neustart in STA-Modus
- **Scan** → löst WLAN-Scan aus, aktualisiert SSID-Dropdown (REST `GET /api/wifi/scan`)

---

## 4  Seitenübersicht (STA-Modus)

```
/                   → Weiterleitung → /status
/status             → Statusseite
/config/wifi        → WLAN & Netzwerk
/config/mqtt        → MQTT
/config/motor       → Motor & Regler
/config/system      → System (Auth, Hostname, Reset)
```

Navigation: Einheitliche Kopfzeile mit Links auf alle Seiten, Gerätename und System-State.

---

## 5  Seiten-Spezifikation

### 5.1  Statusseite (`/status`)

Schreibgeschützt. Aktualisierung: manuell (Reload) oder automatisch per `<meta refresh>` (Intervall konfigurierbar, Default 2 s).

**Anzeige-Blöcke:**

| Block | Felder |
|-------|--------|
| System | System-State, Uptime, Firmware-Version |
| Tür | Door-State, Position [mm], Zielposition [mm] |
| Motor | Ist-Strom [A], PWM-Duty [%], Ist-Geschwindigkeit [mm/s] |
| Netzwerk | IP, SSID, RSSI, MQTT-Verbindungsstatus |
| Fehler | Aktiver Fehlercode + Klartext; Quittier-Button (nur wenn quittierbar) |

---

### 5.2  WLAN & Netzwerk (`/config/wifi`)

| Feld | Param-ID | Typ | Beschreibung |
|------|----------|-----|--------------|
| SSID | — | Text | Gespeichertes Netzwerk (NVS direkt, kein param_id) |
| Passwort | — | Password | Leer = unverändert |
| DHCP | `net_dhcp_en` | Checkbox | Ein = DHCP; Aus = statisch |
| IP-Adresse | `net_ip` | Text | Nur aktiv wenn DHCP aus |
| Subnetzmaske | `net_mask` | Text | Nur aktiv wenn DHCP aus |
| Gateway | `net_gw` | Text | Nur aktiv wenn DHCP aus |
| DNS | `net_dns` | Text | Nur aktiv wenn DHCP aus |
| Hostname | `net_hostname` | Text | mDNS-Name (ohne `.local`) |
| WLAN-Retry | `wifi_retry_max` | uint8 | Versuche vor AP-Fallback |

Aktionen: **Speichern & Neustart** (Neustart erforderlich für Netzwerkänderungen).

---

### 5.3  MQTT (`/config/mqtt`)

| Feld | Param-ID | Typ | Beschreibung |
|------|----------|-----|--------------|
| MQTT aktiviert | `mqtt_en` | Checkbox | — |
| Broker-URL | `mqtt_broker` | Text | z.B. `mqtt://192.168.1.10:1883` |
| Benutzername | `mqtt_user` | Text | Leer = anonym |
| Passwort | `mqtt_pass` | Password | Leer = unverändert |
| Topic-Präfix | `mqtt_topic` | Text | z.B. `hodor/tuer1` |
| QoS | `mqtt_qos` | uint8 (0–2) | Default 0 |
| Retain | `mqtt_retain` | Checkbox | Default false |

Aktionen: **Speichern** (kein Neustart nötig; MQTT-Client reconnectet automatisch).

---

### 5.4  Motor & Regler (`/config/motor`)

Unterteilt in drei Gruppen:

**Gruppe: Mechanik**

| Feld | Param-ID | Typ | Einheit |
|------|----------|-----|---------|
| Öffnungsweg | `door_open_mm` | uint16 | mm |
| Positions-Toleranz | `pos_tolerance_mm` | uint16 | mm |
| Auto-Close | `autoclose_s` | uint16 | s (0 = deaktiviert) |
| Max. Strom | `motor_max_a` | float | A |
| Überstrom-Timeout | `overcurrent_ms` | uint16 | ms |

**Gruppe: Geschwindigkeitsprofil**

| Feld | Param-ID | Typ | Einheit |
|------|----------|-----|---------|
| Max. Geschwindigkeit | `v_max_mms` | float | mm/s |
| Ruckbegrenzer | `jerk_limit_en` | Checkbox | — |
| Max. Ruck | `ramp_jerk` | float | mm/s³ |
| Max. Beschleunigung | `ramp_accel_max` | float | mm/s² |

**Gruppe: Regler-Koeffizienten**

| Feld | Param-ID | Typ | Beschreibung |
|------|----------|-----|-------------|
| Strom Kp | `ctrl_i_kp` | float | Stromregler proportional |
| Strom Ki | `ctrl_i_ki` | float | Stromregler integral |
| Geschwindigkeit Kp | `ctrl_v_kp` | float | Geschwindigkeitsregler proportional |
| Geschwindigkeit Ki | `ctrl_v_ki` | float | Geschwindigkeitsregler integral |
| Position Kp | `ctrl_p_kp` | float | Positionsregler proportional |

Aktionen:
- **Speichern** → `param_set()` + NVS; sofort wirksam (kein Neustart)
- **Defaults laden** → `param_set_default()` für alle Motor-Parameter

---

### 5.5  System (`/config/system`)

| Feld | Param-ID | Typ | Beschreibung |
|------|----------|-----|-------------|
| Auth aktiviert | `web_auth_en` | Checkbox | — |
| Benutzername | `web_user` | Text | — |
| Passwort (neu) | `web_pass` | Password | Leer = unverändert |
| UART-Stream Intervall | `uart_stream_ms` | uint16 | ms |

Aktionen:
- **Speichern**
- **Werksreset** → `param_reset_all()` + NVS löschen + Neustart in AP-Modus (Bestätigungsdialog)
- **Neustart** → ESP-Neustart (Bestätigungsdialog)

---

## 6  REST-API

Basis-URL: `http://<ip>/api/`  
Content-Type: `application/json`  
Auth: wie Webseiten (Basic Auth wenn aktiviert)

### 6.1  Parameter

| Methode | Endpunkt | Beschreibung |
|---------|----------|-------------|
| `GET` | `/api/param/list` | Alle Parameter mit ID, Name, Typ, Wert, Min, Max, Unit |
| `GET` | `/api/param/{id}` | Einzelner Parameter |
| `POST` | `/api/param/{id}` | Wert setzen; Body: `{"val": 1.5}` |
| `POST` | `/api/param/reset` | Alle Parameter auf Default |

### 6.2  Status

| Methode | Endpunkt | Beschreibung |
|---------|----------|-------------|
| `GET` | `/api/status` | Systemzustand, Tür-State, Messwerte |
| `POST` | `/api/cmd` | Steuerbefehl; Body: `{"cmd": "open"}` / `"close"` / `"stop"` / `"fault_clear"` |

### 6.3  WLAN

| Methode | Endpunkt | Beschreibung |
|---------|----------|-------------|
| `GET` | `/api/wifi/scan` | WLAN-Scan starten + Ergebnis (SSID, RSSI, Verschlüsselung) |
| `GET` | `/api/wifi/status` | Aktueller Verbindungsstatus |

### 6.4  System

| Methode | Endpunkt | Beschreibung |
|---------|----------|-------------|
| `POST` | `/api/system/restart` | Neustart |
| `POST` | `/api/system/factory_reset` | Werksreset + Neustart |

### 6.5  Response-Format

Erfolg:
```json
{"ok": true, "val": 1.5}
```

Fehler:
```json
{"ok": false, "err": "out_of_range", "min": 0.0, "max": 5.0}
```

Fehlercodes: `out_of_range`, `unknown_id`, `type_mismatch`, `readonly`, `auth_required`, `invalid_json`

---

## 7  Implementierungshinweise

- ESP-IDF `esp_http_server`: ein Handler pro URI; `httpd_uri_t`-Array statisch definiert
- HTML-Seiten: als `const char[]` im Flash (keine SD-Karte, kein SPIFFS in v1.0)
- Formular-Submit: `POST` mit `application/x-www-form-urlencoded`; serverseitig parsen
- REST-Endpunkte: gleiche Handler-Funktionen wie Formular-Submit; nur Content-Type unterscheidet Response-Format
- Maximale gleichzeitige Verbindungen: 4 (ESP-IDF Default; ausreichend für Einzel-Nutzer)
- Captive-Portal-DNS: `dns_server` Task im AP-Modus; leitet alle DNS-Anfragen auf 192.168.4.1

---

## 8  Noch offen / TBD

| Punkt | Status |
|-------|--------|
| `net_ip`, `net_mask`, `net_gw`, `net_dns`, `net_hostname`, `wifi_retry_max`, `mqtt_user`, `mqtt_pass`, `mqtt_topic`, `mqtt_qos`, `mqtt_retain` | In `parameters.md` noch nicht eingetragen – ergänzen |
| HTTPS / TLS | Explizit nicht in v1.0 (Scope → `architecture.md` Abschnitt 7) |
| OTA über Webserver | Nicht in v1.0 |
| Live-Kurven im Browser (WebSocket) | Nicht in v1.0; UART-Stream als Alternative |
