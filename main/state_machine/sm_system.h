/**
 * @file sm_system.h
 * @brief System-Zustandsmaschine (Start → Init → Standby → Active → Error).
 *
 * sm_task_func() läuft auf Core 1 (Prio 18).
 * Verarbeitet Ereignisse aus sm_event_queue (Quellen: MQTT, HTTP, IO, door_task).
 * Koordiniert Watchdog, Regler und Tür-SM bei Zustandsübergängen.
 */

#pragma once

#include "esp_err.h"
#include "sm_types.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Bits für sys_state_event_group (von comm-Tasks beobachtet) */
#define SYS_EG_INIT_DONE   (1 << 0)
#define SYS_EG_ACTIVE      (1 << 1)
#define SYS_EG_ERROR       (1 << 2)
#define SYS_EG_STANDBY     (1 << 3)

/**
 * @brief System-SM initialisieren.
 *        Erstellt sm_event_queue, sys_state_event_group, comm_status_queue,
 *        init_done_sem.
 */
esp_err_t sm_sys_init(void);

/**
 * @brief sm_task – System-Zustandsmaschine (Core 1, Prio 18).
 *        Blockiert auf sm_event_queue.
 */
void sm_task_func(void *arg);

/**
 * @brief Aktuellen Systemzustand lesen (atomic, ISR-sicher).
 */
sm_sys_state_t sm_sys_get_state(void);

/**
 * @brief Event in sm_event_queue einstellen.
 *        Thread-safe (von Core 0 und Core 1 nutzbar).
 */
esp_err_t sm_sys_post_event(const sm_event_t *evt);

/** @brief Event aus ISR-Kontext einstellen. */
void sm_sys_post_event_from_isr(const sm_event_t *evt);

/** @brief sys_state_event_group Handle (für comm-Tasks). */
EventGroupHandle_t sm_sys_get_event_group(void);

/** @brief init_done_sem Handle (für app_main). */
void *sm_sys_get_init_sem(void);

/** @brief comm_status_queue Handle (für comm_mqtt_task). */
void *sm_sys_get_status_queue(void);

#ifdef __cplusplus
}
#endif
