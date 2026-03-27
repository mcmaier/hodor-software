/**
 * @file sm_door.h
 * @brief Tür-Zustandsmaschine (Closed ↔ Opening ↔ Open ↔ Closing).
 *
 * door_task_func() läuft auf Core 1 (Prio 17).
 * Empfängt Events aus door_event_queue.
 * Steuert ctrl_set_target() und meldet EVT_POS_REACHED / EVT_BLOCKED
 * an sm_task zurück (via sm_sys_post_event).
 */

#pragma once

#include "esp_err.h"
#include "sm_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Tür-SM initialisieren.
 *        Erstellt door_event_queue. Initialzustand: DOOR_UNDEFINED.
 */
esp_err_t sm_door_init(void);

/**
 * @brief door_task – Tür-Zustandsmaschine (Core 1, Prio 17).
 */
void door_task_func(void *arg);

/** @brief Aktuellen Türzustand lesen (atomic). */
sm_door_state_t sm_door_get_state(void);

/**
 * @brief Event in door_event_queue einstellen.
 *        Von sm_task und ctrl_task nutzbar (beide Core 1).
 */
esp_err_t sm_door_post_event(const sm_event_t *evt);

#ifdef __cplusplus
}
#endif
