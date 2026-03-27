/**
 * @file hal_ledc.h
 * @brief HAL – LEDC-PWM-Abstraktion für H-Brücke (20 kHz).
 */

#pragma once

#include "esp_err.h"
#include "driver/ledc.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief LEDC-Timer und Kanäle initialisieren.
 *        PWM-Ausgänge starten mit Duty = 0 (Motor inaktiv).
 */
esp_err_t hal_ledc_init(void);

/**
 * @brief PWM-Duty eines Kanals setzen.
 * @param channel LEDC_CHANNEL_x
 * @param duty    Duty-Cycle [0 .. HODOR_LEDC_DUTY_MAX]
 */
esp_err_t hal_ledc_set_duty(ledc_channel_t channel, uint32_t duty);

/** @brief Beide PWM-Kanäle auf 0 setzen (sicherer Zustand). */
esp_err_t hal_ledc_stop_all(void);

#ifdef __cplusplus
}
#endif
