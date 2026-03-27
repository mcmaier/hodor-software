/**
 * @file sns_task.c
 * @brief Sensing-Task – ADC + Encoder/Endschalter aggregieren.
 */

#include "sns_task.h"
#include "sns_adc.h"
#include "sns_encoder.h"
#include "sns_limit.h"
#include "hodor_config.h"
#include "hodor_param.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_log.h"

static const char *TAG = "sns_task";

static QueueHandle_t  s_data_queue      = NULL;
static SemaphoreHandle_t s_telem_mutex  = NULL;
static sns_data_t     s_telemetry       = {0};

/* Zeitdelta für Geschwindigkeitsberechnung (1 ms = HODOR_CTRL_VEL_DIVIDER Takte) */
#define SNS_VEL_DT_S  (HODOR_CTRL_PERIOD_US * HODOR_CTRL_VEL_DIVIDER * 1e-6f)

esp_err_t sns_task_init(void)
{
    s_data_queue = xQueueCreate(HODOR_QLEN_SNS_DATA, sizeof(sns_data_t));
    if (!s_data_queue) return ESP_ERR_NO_MEM;

    s_telem_mutex = xSemaphoreCreateMutex();
    if (!s_telem_mutex) return ESP_ERR_NO_MEM;

    /* Encoder oder Endschalter je nach Konfiguration */
    param_value_t mode = {0};
    param_get(PARAM_INPUT_MODE_1, &mode);

    esp_err_t ret;
    if (mode.u8 == 3) {
        /* Endschalter-Modus */
        ret = sns_limit_init();
    } else {
        /* Encoder-Modus – TODO: mm_per_count aus Parameter oder Kalibrierung */
        ret = sns_encoder_init(1.0f);
    }
    if (ret != ESP_OK) return ret;

    ret = sns_adc_init();
    if (ret != ESP_OK) return ret;

    ESP_LOGI(TAG, "Sensing initialisiert (Modus: %s)",
             (mode.u8 == 3) ? "Endschalter" : "Encoder");
    return ESP_OK;
}

void sns_task_func(void *arg)
{
    (void)arg;
    uint32_t tick_count = 0;
    sns_data_t data     = {0};

    for (;;) {
        /* Warte auf nächsten Tick (1 ms Periode via FreeRTOS-Delay als Fallback).
         * Synchronisierung mit ctrl_timer wird in ctrl_loop.c behandelt. */
        vTaskDelay(pdMS_TO_TICKS(1));
        tick_count++;

        /* Strommessung jeden Tick */
        sns_adc_get_current_a(&data.current_a);

        /* Geschwindigkeit und Position alle HODOR_CTRL_VEL_DIVIDER Ticks */
        if ((tick_count % HODOR_CTRL_VEL_DIVIDER) == 0) {
            sns_encoder_get_velocity_mms(SNS_VEL_DT_S, &data.velocity_mms);
            sns_encoder_get_position_mm(&data.position_mm);
        }

        /* Daten an ctrl_task – ältester Eintrag wird verworfen wenn voll */
        if (xQueueSend(s_data_queue, &data, 0) != pdTRUE) {
            /* ctrl_task hängt nach – kein fataler Fehler, Daten überspringen */
        }

        /* Telemetrie-Snapshot aktualisieren (für Core-0-Zugriff) */
        if (xSemaphoreTake(s_telem_mutex, 0) == pdTRUE) {
            s_telemetry = data;
            xSemaphoreGive(s_telem_mutex);
        }
    }
}

void sns_get_telemetry(sns_data_t *out)
{
    xSemaphoreTake(s_telem_mutex, portMAX_DELAY);
    *out = s_telemetry;
    xSemaphoreGive(s_telem_mutex);
}

QueueHandle_t sns_get_data_queue(void)
{
    return s_data_queue;
}
