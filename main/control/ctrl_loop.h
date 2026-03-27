/**
 * @file ctrl_loop.h
 * @brief Kaskadenregler – Position → Geschwindigkeit → Strom → PWM.
 *
 * Alle drei Regelkreise laufen in ctrl_task_func() auf Core 1 (Prio 22).
 * Kein dynamischer Speicher zur Laufzeit.
 *
 * Aufteilung via Divider-Zähler:
 *   Jeder Tick: Stromregler (100 µs)
 *   Alle HODOR_CTRL_VEL_DIVIDER Ticks: Geschwindigkeitsregler (1 ms)
 *   Alle HODOR_CTRL_POS_DIVIDER Ticks: Positionsregler (10 ms)
 */

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Regelkreis initialisieren.
 *        Erstellt ctrl_timer_sem. Liest Anfangswerte aus Parametertabelle.
 *        mot_driver_init() muss vorher aufgerufen worden sein.
 */
esp_err_t ctrl_loop_init(void);

/**
 * @brief 100-µs-Timer starten (ctrl_timer_sem periodisch geben).
 *        Muss NACH ctrl_loop_init() und NACH erfolgreicher Hardware-Verifikation
 *        aufgerufen werden (app_main Phase 6).
 */
esp_err_t ctrl_loop_start_timer(void);

/**
 * @brief Regler aktivieren und Integratoren zurücksetzen.
 *        Wird von sm_task beim Eintritt in SYS_ACTIVE aufgerufen.
 */
void ctrl_enable(void);

/**
 * @brief Regler deaktivieren (Motor wird abgeschaltet).
 *        Wird beim Verlassen von SYS_ACTIVE aufgerufen.
 */
void ctrl_disable(void);

/**
 * @brief Zielposition setzen.
 *        Wird von door_task aufgerufen (Opening/Closing).
 * @param target_mm  Zielposition [mm]
 */
void ctrl_set_target(float target_mm);

/**
 * @brief ctrl_task – Regelkreis-Loop (Core 1, Prio 22).
 *        Blockiert auf ctrl_timer_sem (100 µs).
 */
void ctrl_task_func(void *arg);

#ifdef __cplusplus
}
#endif
