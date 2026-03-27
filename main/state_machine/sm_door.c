/**
 * @file sm_door.c
 * @brief Tür-Zustandsmaschine.
 *
 * Zustandsübergänge:
 *
 *   UNDEFINED ──EVT_CMD_OPEN──► OPENING  (Ziel: door_open_mm)
 *   UNDEFINED ──EVT_CMD_CLOSE─► CLOSING  (Ziel: 0 mm)
 *
 *   CLOSED ──EVT_CMD_OPEN/TOGGLE──► OPENING
 *   OPEN   ──EVT_CMD_CLOSE/TOGGLE─► CLOSING
 *
 *   OPENING ──EVT_POS_REACHED──► OPEN
 *   OPENING ──EVT_BLOCKED──────► BLOCKED
 *   OPENING ──EVT_CMD_STOP/EMERGENCY──► CLOSED oder UNDEFINED (je nach Position)
 *   OPENING ──EVT_CMD_CLOSE────► CLOSING  (Reversierung)
 *
 *   CLOSING ──EVT_POS_REACHED──► CLOSED
 *   CLOSING ──EVT_BLOCKED──────► BLOCKED
 *   CLOSING ──EVT_CMD_STOP/EMERGENCY──► OPEN oder UNDEFINED
 *   CLOSING ──EVT_CMD_OPEN─────► OPENING  (Reversierung)
 *
 *   BLOCKED ──EVT_RETRY────────► OPENING oder CLOSING (letztes Ziel wiederholen)
 *   BLOCKED ──EVT_CMD_*────────► entsprechender Zustand (manueller Override)
 */

#include "sm_door.h"
#include "sm_system.h"
#include "ctrl_loop.h"
#include "hodor_config.h"
#include "hodor_param.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/timers.h"
#include "esp_log.h"

static const char *TAG = "sm_door";

static QueueHandle_t        s_event_queue   = NULL;
static volatile sm_door_state_t s_state     = DOOR_STATE_UNDEFINED;
static float                s_last_target   = 0.0f;
static TimerHandle_t        s_autoclose_tmr = NULL;

static void autoclose_timer_cb(TimerHandle_t tmr)
{
    (void)tmr;
    sm_event_t evt = { .id = EVT_CMD_CLOSE, .data = 0 };
    sm_sys_post_event(&evt);
}

static void set_state(sm_door_state_t new_state)
{
    ESP_LOGI(TAG, "Türzustand: %d → %d", (int)s_state, (int)new_state);
    s_state = new_state;
}

static void start_opening(void)
{
    param_value_t v = {0};
    param_get(PARAM_DOOR_OPEN_MM, &v);
    s_last_target = (float)v.u16;
    ctrl_set_target(s_last_target);
    set_state(DOOR_STATE_OPENING);

    /* Autoclose-Timer stoppen (falls laufend) */
    if (s_autoclose_tmr) xTimerStop(s_autoclose_tmr, 0);
}

static void start_closing(void)
{
    s_last_target = 0.0f;
    ctrl_set_target(0.0f);
    set_state(DOOR_STATE_CLOSING);
}

static void process_door_event(const sm_event_t *evt)
{
    switch (s_state) {

        case DOOR_STATE_UNDEFINED:
        case DOOR_STATE_CLOSED:
            if (evt->id == EVT_CMD_OPEN || evt->id == EVT_CMD_TOGGLE)  start_opening();
            if (evt->id == EVT_CMD_CLOSE) start_closing();
            break;

        case DOOR_STATE_OPEN:
            if (evt->id == EVT_CMD_CLOSE || evt->id == EVT_CMD_TOGGLE) start_closing();
            if (evt->id == EVT_CMD_OPEN)  { /* bereits offen */ }
            break;

        case DOOR_STATE_OPENING:
            switch (evt->id) {
                case EVT_POS_REACHED:
                    set_state(DOOR_STATE_OPEN);
                    /* Autoclose-Timer starten wenn konfiguriert */
                    {
                        param_value_t v = {0};
                        param_get(PARAM_AUTOCLOSE_S, &v);
                        if (v.u16 > 0 && s_autoclose_tmr) {
                            xTimerChangePeriod(s_autoclose_tmr,
                                               pdMS_TO_TICKS(v.u16 * 1000u), 0);
                            xTimerStart(s_autoclose_tmr, 0);
                        }
                    }
                    break;
                case EVT_BLOCKED:
                    set_state(DOOR_STATE_BLOCKED);
                    break;
                case EVT_CMD_CLOSE:
                    start_closing();  /* Reversierung */
                    break;
                case EVT_CMD_STOP:
                case EVT_EMERGENCY_STOP:
                    set_state(DOOR_STATE_UNDEFINED);
                    break;
                default: break;
            }
            break;

        case DOOR_STATE_CLOSING:
            switch (evt->id) {
                case EVT_POS_REACHED:
                    set_state(DOOR_STATE_CLOSED);
                    break;
                case EVT_BLOCKED:
                    set_state(DOOR_STATE_BLOCKED);
                    break;
                case EVT_CMD_OPEN:
                case EVT_CMD_TOGGLE:
                    start_opening();  /* Reversierung */
                    break;
                case EVT_CMD_STOP:
                case EVT_EMERGENCY_STOP:
                    set_state(DOOR_STATE_UNDEFINED);
                    break;
                default: break;
            }
            break;

        case DOOR_STATE_BLOCKED:
            switch (evt->id) {
                case EVT_CMD_OPEN:
                case EVT_CMD_TOGGLE:
                    start_opening();
                    /* System muss zurück in ACTIVE – Event an sys-SM */
                    {
                        sm_event_t e = { .id = evt->id, .data = 0 };
                        sm_sys_post_event(&e);
                    }
                    break;
                case EVT_CMD_CLOSE:
                    start_closing();
                    {
                        sm_event_t e = { .id = evt->id, .data = 0 };
                        sm_sys_post_event(&e);
                    }
                    break;
                default: break;
            }
            break;

        default: break;
    }
}

/* =========================================================================
 * API
 * ========================================================================= */
esp_err_t sm_door_init(void)
{
    s_event_queue = xQueueCreate(HODOR_QLEN_DOOR_EVENT, sizeof(sm_event_t));
    if (!s_event_queue) return ESP_ERR_NO_MEM;

    s_autoclose_tmr = xTimerCreate("autoclose", pdMS_TO_TICKS(10000),
                                   pdFALSE, NULL, autoclose_timer_cb);
    if (!s_autoclose_tmr) return ESP_ERR_NO_MEM;

    s_state = DOOR_STATE_UNDEFINED;
    ESP_LOGI(TAG, "Tür-SM initialisiert");
    return ESP_OK;
}

void door_task_func(void *arg)
{
    (void)arg;
    sm_event_t evt;

    for (;;) {
        if (xQueueReceive(s_event_queue, &evt, portMAX_DELAY) == pdTRUE) {
            process_door_event(&evt);
        }
    }
}

sm_door_state_t sm_door_get_state(void)
{
    return s_state;
}

esp_err_t sm_door_post_event(const sm_event_t *evt)
{
    if (!s_event_queue) return ESP_ERR_INVALID_STATE;
    if (xQueueSend(s_event_queue, evt, pdMS_TO_TICKS(5)) != pdTRUE) {
        ESP_LOGW(TAG, "door_event_queue voll");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
