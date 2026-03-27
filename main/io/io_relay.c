/**
 * @file io_relay.c
 * @brief Relais-Ausgang OUT_1.
 */

#include "io_relay.h"
#include "hal_gpio.h"
#include "hodor_config.h"

static bool s_state = false;

esp_err_t io_relay_init(void)
{
    s_state = false;
    hal_gpio_set(HODOR_GPIO_OUT_1, 0);
    return ESP_OK;
}

void io_relay_set(bool on)
{
    s_state = on;
    hal_gpio_set(HODOR_GPIO_OUT_1, on ? 1 : 0);
}

bool io_relay_get(void)
{
    return s_state;
}
