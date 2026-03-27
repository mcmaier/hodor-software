/**
 * @file io_relay.h
 * @brief Schaltbarer Relais-Ausgang (OUT_1).
 */

#pragma once

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Relais-Ausgang initialisieren (initial aus). */
esp_err_t io_relay_init(void);

/** @brief Relais schalten. @param on true = ein */
void io_relay_set(bool on);

/** @brief Aktuellen Relais-Zustand lesen. */
bool io_relay_get(void);

#ifdef __cplusplus
}
#endif
