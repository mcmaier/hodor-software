/**
 * @file hal_gpio.c
 * @brief HAL – GPIO-Initialisierung für HODOR.
 */

#include "hal_gpio.h"
#include "hodor_config.h"
#include "esp_log.h"

static const char *TAG = "hal_gpio";

esp_err_t hal_gpio_init_all(void)
{
    esp_err_t ret = ESP_OK;

    /* GPIO-ISR-Dienst einmalig installieren (benötigt von sns_encoder, io_input) */
    ret = gpio_install_isr_service(0);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        /* ESP_ERR_INVALID_STATE = bereits installiert, kein Fehler */
        ESP_LOGE(TAG, "gpio_install_isr_service: %s", esp_err_to_name(ret));
        return ret;
    }

    /* H-Brücke Ausgänge */
    ret = hal_gpio_init_output(HODOR_GPIO_MOT_EN, 0);
    if (ret != ESP_OK) return ret;

    /* H-Brücke Fault-Eingang (Pull-up, da Open-Drain) */
    ret = hal_gpio_init_input(HODOR_GPIO_MOT_NFAULT, true, false);
    if (ret != ESP_OK) return ret;

    /* Hardware-Watchdog */
    ret = hal_gpio_init_output(HODOR_GPIO_WDG_TRIG, 0);
    if (ret != ESP_OK) return ret;
    /* /CLR low-aktiv: LOW = Watchdog sofort zurückgesetzt (sicherer Anfangszustand) */
    ret = hal_gpio_init_output(HODOR_GPIO_WDG_NCLR, 0);
    if (ret != ESP_OK) return ret;

    /* Relais-Ausgang */
    ret = hal_gpio_init_output(HODOR_GPIO_OUT_1, 0);
    if (ret != ESP_OK) return ret;

    /* Status-LEDs */
    ret = hal_gpio_init_output(HODOR_GPIO_LED_STATUS, 0);
    if (ret != ESP_OK) return ret;
    ret = hal_gpio_init_output(HODOR_GPIO_LED_ACTIVE, 0);
    if (ret != ESP_OK) return ret;
    ret = hal_gpio_init_output(HODOR_GPIO_LED_CONN, 0);
    if (ret != ESP_OK) return ret;

    /* Potentialfreie Eingänge (Pull-up, da externe Kontakte nach GND schalten) */
    ret = hal_gpio_init_input(HODOR_GPIO_IN_1, true, false);
    if (ret != ESP_OK) return ret;
    ret = hal_gpio_init_input(HODOR_GPIO_IN_2, true, false);
    if (ret != ESP_OK) return ret;
    ret = hal_gpio_init_input(HODOR_GPIO_IN_3, true, false);
    if (ret != ESP_OK) return ret;

    /* Encoder-Eingänge */
    ret = hal_gpio_init_input(HODOR_GPIO_ENC_A, true, false);
    if (ret != ESP_OK) return ret;
    ret = hal_gpio_init_input(HODOR_GPIO_ENC_B, true, false);
    if (ret != ESP_OK) return ret;

    ESP_LOGI(TAG, "GPIO initialisiert");
    return ESP_OK;
}

esp_err_t hal_gpio_init_output(int pin, int initial)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << pin),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    esp_err_t ret = gpio_config(&cfg);
    if (ret != ESP_OK) return ret;
    gpio_set_level(pin, initial);
    return ESP_OK;
}

esp_err_t hal_gpio_init_input(int pin, bool pull_up, bool pull_down)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << pin),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = pull_up   ? GPIO_PULLUP_ENABLE   : GPIO_PULLUP_DISABLE,
        .pull_down_en = pull_down ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    return gpio_config(&cfg);
}

void hal_gpio_set(int pin, int level)
{
    gpio_set_level(pin, level);
}

int hal_gpio_get(int pin)
{
    return gpio_get_level(pin);
}
