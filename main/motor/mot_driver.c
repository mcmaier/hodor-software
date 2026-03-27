/**
 * @file mot_driver.c
 * @brief Motor-Treiber – H-Brücke über LEDC PWM.
 *
 * Zwei unabhängige PWM-Kanäle steuern die H-Brücke:
 *   Vorwärts:  CH0 = duty, CH1 = 0
 *   Rückwärts: CH0 = 0,    CH1 = duty
 *   Bremse:    CH0 = max,  CH1 = max
 *   Freilauf:  CH0 = 0,    CH1 = 0
 */

#include "mot_driver.h"
#include "hal_ledc.h"
#include "hal_gpio.h"
#include "hodor_config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include <math.h>

static const char *TAG = "mot_driver";

static mot_state_t      s_state  = {0};
static SemaphoreHandle_t s_mutex = NULL;

esp_err_t mot_driver_init(void)
{
    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) return ESP_ERR_NO_MEM;

    s_state.duty_pct  = 0.0f;
    s_state.direction = MOT_DIR_COAST;
    s_state.enabled   = false;
    s_state.fault     = false;

    /* Sicherer Anfangszustand: Enable low, PWM = 0 */
    hal_gpio_set(HODOR_GPIO_MOT_EN, 0);
    hal_ledc_stop_all();

    ESP_LOGI(TAG, "Motor-Treiber initialisiert");
    return ESP_OK;
}

esp_err_t mot_set_pwm(float duty_pct, mot_dir_t dir)
{
    if (!s_state.enabled) return ESP_OK;  /* ignorieren wenn nicht aktiv */

    if (duty_pct < 0.0f) duty_pct = 0.0f;
    if (duty_pct > 100.0f) duty_pct = 100.0f;

    uint32_t duty_raw = (uint32_t)(duty_pct * HODOR_LEDC_DUTY_MAX / 100.0f);
    esp_err_t ret = ESP_OK;

    switch (dir) {
        case MOT_DIR_FORWARD:
            ret  = hal_ledc_set_duty(HODOR_LEDC_CH_PWM_1, duty_raw);
            ret |= hal_ledc_set_duty(HODOR_LEDC_CH_PWM_2, 0);
            break;
        case MOT_DIR_BACKWARD:
            ret  = hal_ledc_set_duty(HODOR_LEDC_CH_PWM_1, 0);
            ret |= hal_ledc_set_duty(HODOR_LEDC_CH_PWM_2, duty_raw);
            break;
        case MOT_DIR_BRAKE:
            ret  = hal_ledc_set_duty(HODOR_LEDC_CH_PWM_1, HODOR_LEDC_DUTY_MAX);
            ret |= hal_ledc_set_duty(HODOR_LEDC_CH_PWM_2, HODOR_LEDC_DUTY_MAX);
            break;
        case MOT_DIR_COAST:
            ret  = hal_ledc_stop_all();
            break;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_state.duty_pct  = (dir == MOT_DIR_BACKWARD) ? -duty_pct : duty_pct;
    s_state.direction = dir;
    xSemaphoreGive(s_mutex);

    return ret;
}

esp_err_t mot_enable(void)
{
    hal_gpio_set(HODOR_GPIO_MOT_EN, 1);
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_state.enabled = true;
    xSemaphoreGive(s_mutex);
    ESP_LOGD(TAG, "Motor aktiviert");
    return ESP_OK;
}

esp_err_t mot_disable(void)
{
    hal_ledc_stop_all();
    hal_gpio_set(HODOR_GPIO_MOT_EN, 0);
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_state.enabled   = false;
    s_state.duty_pct  = 0.0f;
    s_state.direction = MOT_DIR_COAST;
    xSemaphoreGive(s_mutex);
    ESP_LOGD(TAG, "Motor deaktiviert");
    return ESP_OK;
}

esp_err_t mot_brake(void)
{
    return mot_set_pwm(100.0f, MOT_DIR_BRAKE);
}

esp_err_t mot_coast(void)
{
    return mot_set_pwm(0.0f, MOT_DIR_COAST);
}

void mot_get_state(mot_state_t *out)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    *out = s_state;
    xSemaphoreGive(s_mutex);
}

bool mot_check_fault(void)
{
    /* NFAULT ist low-aktiv: Low → Fault */
    bool fault = (hal_gpio_get(HODOR_GPIO_MOT_NFAULT) == 0);
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_state.fault = fault;
    xSemaphoreGive(s_mutex);
    return fault;
}
