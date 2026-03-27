/**
 * @file comm_uart.h
 * @brief UART-Telemetrie-Stream und Parameter-Get/Set über JSON.
 */

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief UART-Task initialisieren (UART0 bereits durch IDF belegt für Logging). */
esp_err_t comm_uart_init(void);

/**
 * @brief comm_uart_task (Core 0, Prio 9).
 *        Sendet Telemetrie-Stream (PARAM_UART_STREAM_MS Periode).
 *        Empfängt JSON-Befehle (param get/set, stream start/stop).
 */
void comm_uart_task_func(void *arg);

#ifdef __cplusplus
}
#endif
