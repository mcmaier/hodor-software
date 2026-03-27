/**
 * @file hal_adc.h
 * @brief HAL – ADC-Abstraktion für ACS725-Strommessung.
 *
 * Verwendet ESP-IDF ADC Oneshot API. Kalibrierung via eFuse-Kurve wenn
 * verfügbar, sonst Line-Fitting.
 */

#pragma once

#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief ADC-Einheit und Kanal initialisieren, Kalibrierung durchführen.
 *        Muss vor dem ersten hal_adc_read_mv() aufgerufen werden.
 */
esp_err_t hal_adc_init(void);

/**
 * @brief ADC-Messung durchführen.
 * @param[out] out_mv  Messwert in Millivolt (kalibriert)
 * @return ESP_OK bei Erfolg
 */
esp_err_t hal_adc_read_mv(int *out_mv);

/**
 * @brief ADC freigeben (Ressourcen zurückgeben).
 *        Nur für sauberes Herunterfahren nötig.
 */
void hal_adc_deinit(void);

#ifdef __cplusplus
}
#endif
