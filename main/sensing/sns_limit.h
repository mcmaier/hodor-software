/**
 * @file sns_limit.h
 * @brief Endschalter-Auswertung (Alternative zu Encoder).
 *
 * Wenn input_mode_x == 3 (Endschalter-Modus), liefern ENC_A / ENC_B
 * Positionssignale statt Quadratur. Dieses Modul wertet sie aus.
 */

#pragma once

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SNS_LIMIT_NONE    = 0,
    SNS_LIMIT_CLOSED  = (1 << 0),  /**< Endschalter "Tür zu" aktiv */
    SNS_LIMIT_OPEN    = (1 << 1),  /**< Endschalter "Tür auf" aktiv */
} sns_limit_state_t;

/**
 * @brief Endschalter-GPIO initialisieren (keine PCNT, nur GPIO-Pegel).
 *        hal_gpio_init_all() muss vorher aufgerufen worden sein.
 */
esp_err_t sns_limit_init(void);

/**
 * @brief Aktuellen Endschalter-Zustand lesen.
 * @param[out] state  Bitmaske aus sns_limit_state_t
 */
esp_err_t sns_limit_get_state(sns_limit_state_t *state);

#ifdef __cplusplus
}
#endif
