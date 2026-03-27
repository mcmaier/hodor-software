/**
 * @file sm_system.c
 * @brief System-Zustandsmaschine.
 *
 * Zustandsübergänge:
 *
 *   START ──EVT_INIT_OK──► STANDBY
 *   START ──EVT_INIT_FAIL─► ERROR
 *
 *   STANDBY ──EVT_CMD_OPEN/CLOSE/TOGGLE──► ACTIVE
 *   STANDBY ──EVT_FAULT_*──────────────────► ERROR
 *
 *   ACTIVE ──EVT_CMD_STOP──────────────────► STANDBY
 *   ACTIVE ──EVT_EMERGENCY_STOP────────────► STANDBY  (sofortiger Motor-Stop)
 *   ACTIVE ──EVT_FAULT_EVT_FAULT_HBRIDGE   ─► ERROR
 *   ACTIVE ──EVT_CMD_OPEN/CLOSE/TOGGLE──────► ACTIVE  (neues Ziel weiterleiten)
 *   ACTIVE ──EVT_BLOCKED────────────────────► STANDBY (nach Blockier-Handling)
 *   ACTIVE ──EVT_POS_REACHED────────────────► STANDBY
 *
 *   ERROR ──EVT_ERROR_CLEAR──────────────────► STANDBY
 *
 * Wichtig: MQTT-Befehle werden in ALLEN Zuständen (inkl. ACTIVE) verarbeitet.
 */

#include "sm_system.h"
#include "sm_door.h"
#include "ctrl_loop.h"
#include "mot_driver.h"
#include "mot_watchdog.h"
#include "hal_gpio.h"
#include "hodor_config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
#include "esp_log.h"

static const char *TAG = "sm_sys";

static QueueHandle_t     s_event_queue   = NULL;
static QueueHandle_t     s_status_queue  = NULL;
static SemaphoreHandle_t s_init_sem      = NULL;
static EventGroupHandle_t s_event_group  = NULL;
static volatile sm_sys_state_t s_state   = SYS_STATE_START;

/* =========================================================================
 * Init
 * ========================================================================= */
esp_err_t sm_sys_init(void)
{
    s_event_queue = xQueueCreate(HODOR_QLEN_SM_EVENT, sizeof(sm_event_t));
    if (!s_event_queue) return ESP_ERR_NO_MEM;

    s_status_queue = xQueueCreate(HODOR_QLEN_COMM_STATUS, sizeof(comm_status_t));
    if (!s_status_queue) return ESP_ERR_NO_MEM;

    s_init_sem = xSemaphoreCreateBinary();
    if (!s_init_sem) return ESP_ERR_NO_MEM;

    s_event_group = xEventGroupCreate();
    if (!s_event_group) return ESP_ERR_NO_MEM;

    s_state = SYS_STATE_INIT;
    ESP_LOGI(TAG, "System-SM initialisiert");
    return ESP_OK;
}

/* =========================================================================
 * Zustandsübergang
 * ========================================================================= */
static void enter_state(sm_sys_state_t new_state)
{
    sm_sys_state_t old_state = s_state;
    s_state = new_state;
    ESP_LOGI(TAG, "Zustand: %d → %d", (int)old_state, (int)new_state);

    /* Exit-Aktionen */
    if (old_state == SYS_STATE_ACTIVE) {
        ctrl_disable();
        mot_wdg_disable();
        hal_gpio_set(HODOR_GPIO_LED_ACTIVE, 0);
    }

    /* Entry-Aktionen */
    switch (new_state) {
        case SYS_STATE_STANDBY:
            xEventGroupClearBits(s_event_group, SYS_EG_ACTIVE | SYS_EG_ERROR);
            xEventGroupSetBits(s_event_group, SYS_EG_STANDBY);
            hal_gpio_set(HODOR_GPIO_LED_STATUS, 1);
            if (old_state == SYS_STATE_INIT) {
                /* Initialisierung abgeschlossen */
                xEventGroupSetBits(s_event_group, SYS_EG_INIT_DONE);
                xSemaphoreGive(s_init_sem);
            }
            break;

        case SYS_STATE_ACTIVE:
            xEventGroupClearBits(s_event_group, SYS_EG_STANDBY | SYS_EG_ERROR);
            xEventGroupSetBits(s_event_group, SYS_EG_ACTIVE);
            hal_gpio_set(HODOR_GPIO_LED_ACTIVE, 1);
            mot_enable();
            mot_wdg_enable();
            ctrl_enable();
            break;

        case SYS_STATE_ERROR:
            xEventGroupClearBits(s_event_group, SYS_EG_ACTIVE | SYS_EG_STANDBY);
            xEventGroupSetBits(s_event_group, SYS_EG_ERROR);
            hal_gpio_set(HODOR_GPIO_LED_STATUS, 0);
            if (old_state == SYS_STATE_INIT) {
                /* Init fehlgeschlagen – Semaphor NICHT geben, app_main läuft in Timeout */
                ESP_LOGE(TAG, "System-Init fehlgeschlagen");
            }
            break;

        default:
            break;
    }

    /* Status an comm_mqtt_task melden */
    comm_status_t st = {
        .sys_state  = new_state,
        .door_state = sm_door_get_state(),
    };
    xQueueSend(s_status_queue, &st, 0);
}

/* =========================================================================
 * Event-Verarbeitung
 * ========================================================================= */
static void process_event(const sm_event_t *evt)
{
    switch (s_state) {

        case SYS_STATE_INIT:
            if (evt->id == EVT_INIT_OK)   enter_state(SYS_STATE_STANDBY);
            if (evt->id == EVT_INIT_FAIL) enter_state(SYS_STATE_ERROR);
            break;

        case SYS_STATE_STANDBY:
            switch (evt->id) {
                case EVT_CMD_OPEN:
                case EVT_CMD_CLOSE:
                case EVT_CMD_TOGGLE:
                    enter_state(SYS_STATE_ACTIVE);
                    sm_door_post_event(evt);  /* Tür-SM startet Fahrt */
                    break;
                case EVT_FAULT_OVERCURRENT:
                case EVT_FAULT_HBRIDGE:
                    enter_state(SYS_STATE_ERROR);
                    break;
                default: break;
            }
            break;

        case SYS_STATE_ACTIVE:
            /* MQTT-Befehle müssen hier vollständig verarbeitet werden! */
            switch (evt->id) {
                case EVT_CMD_STOP:
                case EVT_EMERGENCY_STOP:
                    sm_door_post_event(evt);
                    enter_state(SYS_STATE_STANDBY);
                    break;

                case EVT_CMD_OPEN:
                case EVT_CMD_CLOSE:
                case EVT_CMD_TOGGLE:
                    /* Neues Fahrziel während Fahrt – direkt an Tür-SM weiterleiten */
                    sm_door_post_event(evt);
                    break;

                case EVT_POS_REACHED:
                    sm_door_post_event(evt);
                    enter_state(SYS_STATE_STANDBY);
                    break;

                case EVT_BLOCKED:
                    sm_door_post_event(evt);
                    enter_state(SYS_STATE_STANDBY);
                    break;

                case EVT_FAULT_OVERCURRENT:
                case EVT_FAULT_HBRIDGE:
                    enter_state(SYS_STATE_ERROR);
                    break;

                default: break;
            }
            break;

        case SYS_STATE_ERROR:
            if (evt->id == EVT_ERROR_CLEAR) enter_state(SYS_STATE_STANDBY);
            break;

        default: break;
    }
}

/* =========================================================================
 * sm_task
 * ========================================================================= */
void sm_task_func(void *arg)
{
    (void)arg;
    sm_event_t evt;

    /* H-Brücke prüfen und INIT abschließen */
    bool fault = mot_check_fault();
    sm_event_t init_evt = { .id = fault ? EVT_INIT_FAIL : EVT_INIT_OK, .data = 0 };
    xQueueSend(s_event_queue, &init_evt, portMAX_DELAY);

    for (;;) {
        if (xQueueReceive(s_event_queue, &evt, portMAX_DELAY) == pdTRUE) {
            process_event(&evt);
        }
    }
}

/* =========================================================================
 * Accessor-Funktionen
 * ========================================================================= */
sm_sys_state_t sm_sys_get_state(void)
{
    return s_state;  /* 32-Bit-Read auf Xtensa atomic */
}

esp_err_t sm_sys_post_event(const sm_event_t *evt)
{
    if (!s_event_queue) return ESP_ERR_INVALID_STATE;
    if (xQueueSend(s_event_queue, evt, pdMS_TO_TICKS(10)) != pdTRUE) {
        ESP_LOGW(TAG, "sm_event_queue voll – Event %d verloren", evt->id);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void sm_sys_post_event_from_isr(const sm_event_t *evt)
{
    BaseType_t woken = pdFALSE;
    xQueueSendFromISR(s_event_queue, evt, &woken);
    if (woken) portYIELD_FROM_ISR();
}

EventGroupHandle_t sm_sys_get_event_group(void)
{
    return s_event_group;
}

void *sm_sys_get_init_sem(void)
{
    return s_init_sem;
}

void *sm_sys_get_status_queue(void)
{
    return s_status_queue;
}
