/**
 * @file hal_pcnt.h
 * @brief HAL – PCNT-Quadraturdecoder für Encoder.
 *
 * Konfiguriert PCNT-Einheit für quadrature decoding auf ENC_A / ENC_B.
 * Stellt Zählerstand und Überlauf-Tracking bereit.
 */

#pragma once

#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief PCNT-Einheit für Quadratur-Encoder initialisieren.
 *        Pinzuweisung aus hodor_config.h (ENC_A / ENC_B).
 */
esp_err_t hal_pcnt_init(void);

/**
 * @brief Zählerstand auslesen (32-Bit akkumuliert inkl. Überlauf-Tracking).
 * @param[out] count  Akkumulierter Encoder-Zählerstand
 */
esp_err_t hal_pcnt_get_count(int32_t *count);

/** @brief Zähler auf 0 zurücksetzen. */
esp_err_t hal_pcnt_clear(void);

/** @brief PCNT-Ressourcen freigeben. */
void hal_pcnt_deinit(void);

#ifdef __cplusplus
}
#endif
