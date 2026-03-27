/**
 * @file hal_ledc.c
 * @brief HAL – LEDC PWM-Initialisierung für H-Brücke.
 */

#include "hal_ledc.h"
#include "hodor_config.h"
#include "esp_log.h"

static const char *TAG = "hal_ledc";

esp_err_t hal_ledc_init(void)
{
    esp_err_t ret;

    ledc_timer_config_t timer_cfg = {
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .duty_resolution = HODOR_LEDC_RESOLUTION,
        .timer_num       = HODOR_LEDC_TIMER,
        .freq_hz         = HODOR_PWM_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ret = ledc_timer_config(&timer_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ledc_timer_config: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Kanal 0 – MOT_PWM_1 */
    ledc_channel_config_t ch0 = {
        .gpio_num   = HODOR_GPIO_MOT_PWM_1,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel    = HODOR_LEDC_CH_PWM_1,
        .intr_type  = LEDC_INTR_DISABLE,
        .timer_sel  = HODOR_LEDC_TIMER,
        .duty       = 0,
        .hpoint     = 0,
    };
    ret = ledc_channel_config(&ch0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ledc_channel_config ch0: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Kanal 1 – MOT_PWM_2 */
    ledc_channel_config_t ch1 = {
        .gpio_num   = HODOR_GPIO_MOT_PWM_2,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel    = HODOR_LEDC_CH_PWM_2,
        .intr_type  = LEDC_INTR_DISABLE,
        .timer_sel  = HODOR_LEDC_TIMER,
        .duty       = 0,
        .hpoint     = 0,
    };
    ret = ledc_channel_config(&ch1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ledc_channel_config ch1: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "LEDC %u Hz, %u Bit initialisiert",
             HODOR_PWM_FREQ_HZ, HODOR_LEDC_RESOLUTION);
    return ESP_OK;
}

esp_err_t hal_ledc_set_duty(ledc_channel_t channel, uint32_t duty)
{
    esp_err_t ret;
    ret = ledc_set_duty(LEDC_LOW_SPEED_MODE, channel, duty);
    if (ret != ESP_OK) return ret;
    return ledc_update_duty(LEDC_LOW_SPEED_MODE, channel);
}

esp_err_t hal_ledc_stop_all(void)
{
    esp_err_t r1 = hal_ledc_set_duty(HODOR_LEDC_CH_PWM_1, 0);
    esp_err_t r2 = hal_ledc_set_duty(HODOR_LEDC_CH_PWM_2, 0);
    return (r1 != ESP_OK) ? r1 : r2;
}
