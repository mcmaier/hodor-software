/**
 * @file mot_driver.h
 * @brief Motor-Treiber – H-Brücke, PWM-Ausgabe, Enable/Disable.
 *
 * Schreibt mot_state_t. Zugriff für Telemetrie via mot_get_state().
 * Aufruf ausschließlich aus ctrl_task (Core 1).
 */

#pragma once

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MOT_DIR_FORWARD  = 0,
    MOT_DIR_BACKWARD = 1,
    MOT_DIR_BRAKE    = 2,
    MOT_DIR_COAST    = 3,
} mot_dir_t;

/** MC33HB2002 SPI-Status (aufgelöste Fault-Bits). */
typedef struct {
    bool ot;     /**< Übertemperatur-Abschaltung (latching) */
    bool tw;     /**< Temperaturwarnung (non-latching) */
    bool oc;     /**< Overcurrent (latching) */
    bool ol;     /**< Open Load */
    bool scg1;   /**< Kurzschluss nach GND, OUT1 (latching) */
    bool scg2;   /**< Kurzschluss nach GND, OUT2 (latching) */
    bool scp1;   /**< Kurzschluss nach VPWR, OUT1 (latching) */
    bool scp2;   /**< Kurzschluss nach VPWR, OUT2 (latching) */
    bool ov;     /**< Überspannung (non-latching) */
    bool uv;     /**< Unterspannung (non-latching) */
    bool cp_u;   /**< Ladepumpen-Unterspannung */
    bool frm;    /**< SPI Framing Error */
} mot_spi_status_t;

typedef struct {
    float     duty_pct;   /**< Aktueller Duty-Cycle [−100 … +100 %] */
    mot_dir_t direction;
    bool      enabled;
    bool      fault;      /**< H-Brücken-Fault (NFAULT-Signal oder SPI) */
    mot_spi_status_t spi_status; /**< Detaillierter SPI-Fehlerstatus */
} mot_state_t;

/**
 * @brief Motortreiber initialisieren (LEDC-Ausgaben auf 0, Enable low).
 *        hal_ledc_init() und hal_gpio_init_all() müssen vorher aufgerufen worden sein.
 */
esp_err_t mot_driver_init(void);

/**
 * @brief PWM-Duty und Richtung setzen.
 * @param duty_pct  Duty-Cycle [0.0 … 100.0] – Vorzeichen wird ignoriert
 * @param dir       Richtung / Bremsmodus
 * @note  Nur wenn mot_enabled; andernfalls wird der Aufruf ignoriert.
 */
esp_err_t mot_set_pwm(float duty_pct, mot_dir_t dir);

/** @brief H-Brücke aktivieren (MOT_EN high). */
esp_err_t mot_enable(void);

/** @brief H-Brücke deaktivieren, PWM auf 0 (MOT_EN low). */
esp_err_t mot_disable(void);

/** @brief Aktive Bremse (beide Low-Side-FETs ein). */
esp_err_t mot_brake(void);

/** @brief Freilauf (alle FETs aus). */
esp_err_t mot_coast(void);

/**
 * @brief Aktuellen Motorzustand lesen (für Telemetrie).
 *        Thread-safe via mot_state_mutex.
 */
void mot_get_state(mot_state_t *out);

/**
 * @brief NFAULT-Pin UND SPI-Statusregister auslesen.
 *        Aktualisiert fault-Flag und spi_status in mot_state_t.
 */
bool mot_check_fault(void);

/**
 * @brief Alle latching Faults im MC33HB2002 per SPI löschen.
 *        Nur aufrufen wenn Fehlerursache beseitigt ist.
 */
esp_err_t mot_clear_faults(void);

#ifdef __cplusplus
}
#endif
