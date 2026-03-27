/**
 * @file sns_encoder.h
 * @brief Quadraturencoder – Position und Geschwindigkeit.
 */

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Encoder-Modul initialisieren.
 *        hal_pcnt_init() muss vorher aufgerufen worden sein.
 * @param mm_per_count  Umrechnungsfaktor [mm pro Encoder-Zähler]
 */
esp_err_t sns_encoder_init(float mm_per_count);

/** @brief Position in mm lesen. */
esp_err_t sns_encoder_get_position_mm(float *pos_mm);

/**
 * @brief Geschwindigkeit berechnen (Differenz zum letzten Aufruf).
 *        Muss mit gleichmäßigem Zeitraster aufgerufen werden.
 * @param dt_s      Zeitdelta seit letztem Aufruf [s]
 * @param vel_mms   Geschwindigkeit [mm/s]
 */
esp_err_t sns_encoder_get_velocity_mms(float dt_s, float *vel_mms);

/** @brief Encoder-Nullpunkt setzen (aktuelle Position = 0). */
esp_err_t sns_encoder_reset(void);

#ifdef __cplusplus
}
#endif
