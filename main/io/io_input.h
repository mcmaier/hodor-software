/**
 * @file io_input.h
 * @brief Potentialfreie Steuereingänge IN_1 / IN_2 / IN_3.
 *
 * Jeder Eingang ist über PARAM_INPUT_MODE_x konfigurierbar:
 *   0 = Taster   (Flanke → Event)
 *   1 = Schalter (Pegel  → Zustand)
 *   2 = Bewegungsmelder (High-Flanke → Öffnen, Timeout → Schließen)
 *   3 = Endschalter (Pegel → EVT_POS_REACHED, wird von sns_limit behandelt)
 *
 * io_task_func() läuft auf Core 1 (Prio 16).
 */

#pragma once

#include "esp_err.h"
#include "sm_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Eingänge initialisieren (ISR installieren, Queue erstellen).
 *        hal_gpio_init_all() und gpio_install_isr_service() müssen vorher aufgerufen sein.
 */
esp_err_t io_input_init(void);

/**
 * @brief io_task – verarbeitet GPIO-ISR-Events und leitet sm_event_t ab.
 *        Core 1, Prio 16.
 */
void io_task_func(void *arg);

#ifdef __cplusplus
}
#endif
