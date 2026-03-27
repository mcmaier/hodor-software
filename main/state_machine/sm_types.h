/**
 * @file sm_types.h
 * @brief Gemeinsame Typen für System- und Tür-Zustandsmaschinen.
 *
 * Dieses Header wird von allen Modulen eingebunden, die Events erzeugen
 * oder konsumieren (sm, door, ctrl, io, comm). Keine Implementierung hier.
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * System-Zustände
 * ========================================================================= */
typedef enum {
    SYS_STATE_START   = 0,
    SYS_STATE_INIT    = 1,
    SYS_STATE_STANDBY = 2,
    SYS_STATE_ACTIVE  = 3,
    SYS_STATE_ERROR   = 4,
} sm_sys_state_t;

/* =========================================================================
 * Tür-Zustände
 * ========================================================================= */
typedef enum {
    DOOR_STATE_UNDEFINED = 0,
    DOOR_STATE_CLOSED    = 1,
    DOOR_STATE_OPENING   = 2,
    DOOR_STATE_OPEN      = 3,
    DOOR_STATE_CLOSING   = 4,
    DOOR_STATE_BLOCKED   = 5,
    DOOR_STATE_ERROR     = 6,
} sm_door_state_t;

/* =========================================================================
 * Event-IDs (für sm_event_queue und door_event_queue)
 * ========================================================================= */
typedef enum {
    EVT_NONE = 0,

    /* Initialisierung */
    EVT_INIT_OK,
    EVT_INIT_FAIL,

    /* Befehle – von MQTT / HTTP / IO */
    EVT_CMD_OPEN,
    EVT_CMD_CLOSE,
    EVT_CMD_STOP,
    EVT_CMD_TOGGLE,
    EVT_EMERGENCY_STOP,

    /* Hardware-Ereignisse */
    EVT_FAULT_OVERCURRENT,
    EVT_FAULT_HBRIDGE,
    EVT_ERROR_CLEAR,

    /* Regler-Rückmeldungen (Core 1 intern → door_event_queue) */
    EVT_POS_REACHED,
    EVT_BLOCKED,
    EVT_AUTOCLOSE_TIMEOUT,
    EVT_RETRY,
} sm_event_id_t;

/** Event-Nachricht, die in sm_event_queue und door_event_queue übertragen wird. */
typedef struct {
    sm_event_id_t id;
    int32_t       data;   /**< Optionaler Payload (z.B. Zielposition in mm) */
} sm_event_t;

/* =========================================================================
 * Status-Nachricht (Core 1 → comm_status_queue → comm_mqtt_task)
 * ========================================================================= */
typedef struct {
    sm_sys_state_t  sys_state;
    sm_door_state_t door_state;
    float           position_mm;
    float           current_a;
} comm_status_t;

#ifdef __cplusplus
}
#endif
