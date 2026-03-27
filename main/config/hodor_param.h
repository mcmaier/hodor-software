/**
 * @file hodor_param.h
 * @brief Parameter-Framework – einziger Zugriffspunkt für alle Laufzeitparameter.
 *
 * Kein Modul hält eigene Kopien von Parameterwerten. Ausschließlich
 * param_get() / param_set() verwenden. Mutex-Schutz ist intern.
 */

#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * Typen
 * ========================================================================= */
typedef enum {
    PARAM_TYPE_FLOAT  = 0,
    PARAM_TYPE_UINT16 = 1,
    PARAM_TYPE_UINT8  = 2,
    PARAM_TYPE_BOOL   = 3,
} param_type_t;

typedef union {
    float    f;
    uint16_t u16;
    uint8_t  u8;
    uint8_t  b;   /* bool */
} param_value_t;

/* Flags */
#define PARAM_FLAG_PERSIST   (1u << 0)  /**< In NVS speichern/laden */
#define PARAM_FLAG_READONLY  (1u << 1)  /**< Nur lesbar */
#define PARAM_FLAG_STREAM    (1u << 2)  /**< UART-Telemetrie-Stream */
#define PARAM_FLAG_REBOOT    (1u << 3)  /**< Neustart nötig */

typedef struct {
    uint16_t       id;
    param_type_t   type;
    const char    *name;
    const char    *unit;
    const char    *nvs_key;
    param_value_t  val;    /**< Aktueller Wert (RAM, Mutex-geschützt) */
    param_value_t  def;    /**< Defaultwert */
    param_value_t  min;
    param_value_t  max;
    uint8_t        flags;
} param_desc_t;

/* =========================================================================
 * Parameter-IDs
 * ========================================================================= */
typedef enum {
    /* Block 0x0000 – Motor */
    PARAM_MOTOR_MAX_A         = 0x0001,
    PARAM_MOTOR_PWM_FREQ_HZ   = 0x0002,
    PARAM_OVERCURRENT_MS      = 0x0003,

    /* Block 0x0100 – Tür / Position */
    PARAM_DOOR_OPEN_MM        = 0x0101,
    PARAM_POS_TOLERANCE_MM    = 0x0102,
    PARAM_AUTOCLOSE_S         = 0x0103,

    /* Block 0x0200 – Regler */
    PARAM_CTRL_I_KP           = 0x0201,
    PARAM_CTRL_I_KI           = 0x0202,
    PARAM_CTRL_V_KP           = 0x0203,
    PARAM_CTRL_V_KI           = 0x0204,
    PARAM_CTRL_P_KP           = 0x0205,
    PARAM_V_MAX_MMS           = 0x0206,
    PARAM_V_MIN_THRESH_MMS    = 0x0207,

    /* Block 0x0300 – Ruckbegrenzer */
    PARAM_JERK_LIMIT_EN       = 0x0301,
    PARAM_RAMP_JERK           = 0x0302,
    PARAM_RAMP_ACCEL_MAX      = 0x0303,

    /* Block 0x0400 – I/O-Konfiguration */
    PARAM_INPUT_MODE_1        = 0x0401,
    PARAM_INPUT_MODE_2        = 0x0402,
    PARAM_INPUT_MODE_3        = 0x0403,
    PARAM_INPUT_MODE_4        = 0x0404,

    /* Block 0x0500 – Kommunikation */
    PARAM_MQTT_EN             = 0x0501,
    PARAM_UART_STREAM_MS      = 0x0502,

    /* Block 0x0F00 – Telemetrie (Read-only, kein NVS) */
    PARAM_MEAS_CURRENT_A      = 0x0F01,
    PARAM_MEAS_VELOCITY_MMS   = 0x0F02,
    PARAM_MEAS_POSITION_MM    = 0x0F03,
    PARAM_MEAS_PWM_DUTY_PCT   = 0x0F04,
    PARAM_MEAS_SYS_STATE      = 0x0F05,
    PARAM_MEAS_DOOR_STATE     = 0x0F06,
} param_id_t;

/* =========================================================================
 * API
 * ========================================================================= */

/**
 * @brief Parametertabelle initialisieren und NVS-Werte laden.
 *        Muss nach cfg_nvs_init() und vor allen anderen Modul-Inits aufgerufen werden.
 */
esp_err_t param_init(void);

/**
 * @brief Parameterwert lesen (Mutex-geschützt).
 * @return ESP_ERR_NOT_FOUND wenn ID unbekannt
 */
esp_err_t param_get(param_id_t id, param_value_t *val);

/**
 * @brief Parameterwert schreiben mit Limit-Prüfung (Mutex-geschützt).
 * @return ESP_ERR_NOT_FOUND     – ID unbekannt
 * @return ESP_ERR_NOT_SUPPORTED – READONLY-Flag gesetzt
 * @return ESP_ERR_INVALID_ARG   – außerhalb [min, max]
 */
esp_err_t param_set(param_id_t id, param_value_t val);

/** @brief Einen Parameter auf Defaultwert zurücksetzen. */
esp_err_t param_set_default(param_id_t id);

/** @brief Alle Parameter auf Default; NVS wird nicht automatisch gelöscht. */
esp_err_t param_reset_all(void);

/**
 * @brief Einzelnen Parameter in NVS schreiben (nur wenn PARAM_FLAG_PERSIST).
 *        Blockiert bis NVS-Commit abgeschlossen.
 */
esp_err_t param_save(param_id_t id);

/** @brief Alle persistenten Parameter in NVS schreiben. */
esp_err_t param_save_all(void);

/**
 * @brief Pointer auf param_desc_t für UART/JSON-Ausgabe.
 * @return NULL wenn ID unbekannt
 */
const param_desc_t *param_get_desc(param_id_t id);

/** @brief Anzahl der Einträge in param_table[] (für Iteration). */
size_t param_count(void);

/** @brief Pointer auf Eintrag i in param_table[] (0-basiert). */
const param_desc_t *param_get_by_index(size_t index);

#ifdef __cplusplus
}
#endif
