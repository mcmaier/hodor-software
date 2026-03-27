/**
 * @file sns_limit.c
 * @brief Endschalter-Auswertung via GPIO-Pegel.
 *
 * ENC_A und ENC_B werden im Endschalter-Modus als einfache Digitaleingänge
 * genutzt (Pull-up, Kontakt nach GND).
 */

#include "sns_limit.h"
#include "hal_gpio.h"
#include "hodor_config.h"
#include "esp_log.h"

static const char *TAG = "sns_limit";

esp_err_t sns_limit_init(void)
{
    /* Pins wurden bereits in hal_gpio_init_all() als Input mit Pull-up konfiguriert */
    ESP_LOGI(TAG, "Endschalter-Modus aktiv (GPIO %d / %d)",
             HODOR_GPIO_ENC_A, HODOR_GPIO_ENC_B);
    return ESP_OK;
}

esp_err_t sns_limit_get_state(sns_limit_state_t *state)
{
    *state = SNS_LIMIT_NONE;
    /* Low-aktiv (Pull-up, Schalter nach GND) */
    if (hal_gpio_get(HODOR_GPIO_ENC_A) == 0) *state |= SNS_LIMIT_CLOSED;
    if (hal_gpio_get(HODOR_GPIO_ENC_B) == 0) *state |= SNS_LIMIT_OPEN;
    return ESP_OK;
}
