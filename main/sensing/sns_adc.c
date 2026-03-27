/**
 * @file sns_adc.c
 * @brief ACS725-Strommessung mit Oversampling und Offset-Kalibrierung.
 */

#include "sns_adc.h"
#include "hal_adc.h"
#include "hodor_config.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "sns_adc";

static float s_offset_mv = HODOR_ACS725_MIDPOINT_V * 1000.0f; /* Initialwert */

esp_err_t sns_adc_init(void)
{
    /* Offset-Kalibrierung: HODOR_ADC_OVERSAMPLE Messungen bei Motor still */
    float sum_mv = 0.0f;
    for (int i = 0; i < HODOR_ADC_OVERSAMPLE; i++) {
        int mv = 0;
        esp_err_t ret = hal_adc_read_mv(&mv);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "ADC-Kalibrierung fehlgeschlagen: %s", esp_err_to_name(ret));
            return ret;
        }
        sum_mv += (float)mv;
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    s_offset_mv = sum_mv / HODOR_ADC_OVERSAMPLE;
    ESP_LOGI(TAG, "ACS725 Offset: %.1f mV (erwartet ~%.0f mV)",
             s_offset_mv, HODOR_ACS725_MIDPOINT_V * 1000.0f);
    return ESP_OK;
}

esp_err_t sns_adc_get_current_a(float *current_a)
{
    float sum_mv = 0.0f;
    for (int i = 0; i < HODOR_ADC_OVERSAMPLE; i++) {
        int mv = 0;
        esp_err_t ret = hal_adc_read_mv(&mv);
        if (ret != ESP_OK) return ret;
        sum_mv += (float)mv;
    }
    float avg_mv  = sum_mv / HODOR_ADC_OVERSAMPLE;
    *current_a    = (avg_mv - s_offset_mv) / (HODOR_ACS725_SENSITIVITY_V_PER_A * 1000.0f);
    return ESP_OK;
}
