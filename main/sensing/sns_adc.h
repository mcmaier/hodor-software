/**
 * @file sns_adc.h
 * @brief Strommessung über ACS725 + ADC.
 */

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief ADC-Kanal initialisieren und Nullpunkt-Offset kalibrieren.
 *        hal_adc_init() muss vorher aufgerufen worden sein.
 *        Misst HODOR_ADC_OVERSAMPLE Werte bei stehendem Motor → Offset.
 */
esp_err_t sns_adc_init(void);

/**
 * @brief Strom messen (blockierend, aus Task-Kontext aufrufen).
 *        Mittelt HODOR_ADC_OVERSAMPLE Messungen.
 * @param[out] current_a  Strom in Ampere (positiv = Vorwärts)
 */
esp_err_t sns_adc_get_current_a(float *current_a);

#ifdef __cplusplus
}
#endif
