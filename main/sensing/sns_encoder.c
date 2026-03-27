/**
 * @file sns_encoder.c
 * @brief Quadraturencoder – Positions- und Geschwindigkeitsberechnung.
 */

#include "sns_encoder.h"
#include "hal_pcnt.h"
#include "esp_log.h"

static const char *TAG = "sns_enc";

static float   s_mm_per_count  = 1.0f;
static int32_t s_last_count    = 0;

esp_err_t sns_encoder_init(float mm_per_count)
{
    if (mm_per_count <= 0.0f) return ESP_ERR_INVALID_ARG;
    s_mm_per_count = mm_per_count;
    s_last_count   = 0;
    ESP_LOGI(TAG, "Encoder init: %.4f mm/count", mm_per_count);
    return hal_pcnt_clear();
}

esp_err_t sns_encoder_get_position_mm(float *pos_mm)
{
    int32_t count = 0;
    esp_err_t ret = hal_pcnt_get_count(&count);
    if (ret != ESP_OK) return ret;
    *pos_mm = (float)count * s_mm_per_count;
    return ESP_OK;
}

esp_err_t sns_encoder_get_velocity_mms(float dt_s, float *vel_mms)
{
    if (dt_s <= 0.0f) return ESP_ERR_INVALID_ARG;
    int32_t count = 0;
    esp_err_t ret = hal_pcnt_get_count(&count);
    if (ret != ESP_OK) return ret;
    int32_t delta  = count - s_last_count;
    s_last_count   = count;
    *vel_mms       = ((float)delta * s_mm_per_count) / dt_s;
    return ESP_OK;
}

esp_err_t sns_encoder_reset(void)
{
    s_last_count = 0;
    return hal_pcnt_clear();
}
