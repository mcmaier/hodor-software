/**
 * @file sns_task.h
 * @brief Sensing-Task – aggregiert ADC und Encoder/Endschalter.
 *
 * sns_task_func() läuft auf Core 1 (Prio 20).
 * Produziert sns_data_t in sns_data_queue → konsumiert von ctrl_task.
 * Schreibt Telemetrie-Snapshot (sns_telemetry_mutex-geschützt) für HTTP/UART.
 */

#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Vom sns_task an ctrl_task übertragene Messdaten. */
typedef struct {
    float current_a;    /**< Strom [A] */
    float velocity_mms; /**< Geschwindigkeit [mm/s] */
    float position_mm;  /**< Position [mm] */
} sns_data_t;

/**
 * @brief Sensing-Modul initialisieren.
 *        Erstellt sns_data_queue und sns_telemetry_mutex.
 *        Wählt Encoder oder Endschalter-Modus anhand PARAM_INPUT_MODE_x.
 */
esp_err_t sns_task_init(void);

/**
 * @brief sns_task – Sensing-Loop (Core 1, Prio 20).
 *        Trigger: ctrl_timer_sem (100 µs) oder eigenständig (1 ms).
 */
void sns_task_func(void *arg);

/**
 * @brief Telemetrie-Snapshot lesen (mutex-geschützt).
 *        Für HTTP/UART-Task auf Core 0.
 */
void sns_get_telemetry(sns_data_t *out);

/**
 * @brief Queue-Handle für ctrl_task (sns_data_t-Elemente).
 *        Wird in sns_task_init() erstellt.
 */
QueueHandle_t sns_get_data_queue(void);

#ifdef __cplusplus
}
#endif
