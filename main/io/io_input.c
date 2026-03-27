/**
 * @file io_input.c
 * @brief Potentialfreie Eingänge mit konfigurierbarem Verhalten.
 *
 * ISR → io_isr_queue → io_task_func → sm_event_queue
 */

#include "io_input.h"
#include "sm_system.h"
#include "hodor_config.h"
#include "hodor_param.h"
#include "hal_gpio.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "io_input";

/* Rohdaten aus ISR */
typedef struct {
    int  pin;
    int  level;
    int64_t timestamp_us;
} io_raw_event_t;

static QueueHandle_t s_isr_queue = NULL;

static const int s_input_pins[3] = {
    HODOR_GPIO_IN_1,
    HODOR_GPIO_IN_2,
    HODOR_GPIO_IN_3,
};

static void IRAM_ATTR gpio_isr_handler(void *arg)
{
    io_raw_event_t evt = {
        .pin          = (int)(intptr_t)arg,
        .level        = gpio_get_level((gpio_num_t)(intptr_t)arg),
        .timestamp_us = esp_timer_get_time(),
    };
    BaseType_t woken = pdFALSE;
    xQueueSendFromISR(s_isr_queue, &evt, &woken);
    if (woken) portYIELD_FROM_ISR();
}

esp_err_t io_input_init(void)
{
    s_isr_queue = xQueueCreate(HODOR_QLEN_IO_ISR, sizeof(io_raw_event_t));
    if (!s_isr_queue) return ESP_ERR_NO_MEM;

    for (int i = 0; i < 3; i++) {
        gpio_config_t cfg = {
            .pin_bit_mask = (1ULL << s_input_pins[i]),
            .mode         = GPIO_MODE_INPUT,
            .pull_up_en   = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type    = GPIO_INTR_ANYEDGE,
        };
        esp_err_t ret = gpio_config(&cfg);
        if (ret != ESP_OK) return ret;

        ret = gpio_isr_handler_add(s_input_pins[i], gpio_isr_handler,
                                   (void *)(intptr_t)s_input_pins[i]);
        if (ret != ESP_OK) return ret;
    }

    ESP_LOGI(TAG, "IO-Eingänge initialisiert");
    return ESP_OK;
}

/* Entprellzeit */
#define DEBOUNCE_US  20000LL   /* 20 ms */

void io_task_func(void *arg)
{
    (void)arg;
    io_raw_event_t evt;
    int64_t last_event_us[3] = {0};

    for (;;) {
        if (xQueueReceive(s_isr_queue, &evt, portMAX_DELAY) != pdTRUE) continue;

        /* Eingangs-Index bestimmen */
        int idx = -1;
        for (int i = 0; i < 3; i++) {
            if (s_input_pins[i] == evt.pin) { idx = i; break; }
        }
        if (idx < 0) continue;

        /* Entprellung */
        if ((evt.timestamp_us - last_event_us[idx]) < DEBOUNCE_US) continue;
        last_event_us[idx] = evt.timestamp_us;

        /* Modus aus Parametertabelle */
        param_value_t mode = {0};
        param_get((param_id_t)(PARAM_INPUT_MODE_1 + idx), &mode);

        sm_event_t sm_evt = { .id = EVT_NONE, .data = 0 };

        switch (mode.u8) {
            case 0: /* Taster – fallende Flanke = Betätigung */
                if (evt.level == 0) sm_evt.id = EVT_CMD_TOGGLE;
                break;

            case 1: /* Schalter – Low = Öffnen, High = Schließen */
                sm_evt.id = (evt.level == 0) ? EVT_CMD_OPEN : EVT_CMD_CLOSE;
                break;

            case 2: /* Bewegungsmelder – steigende Flanke = Öffnen */
                if (evt.level == 1) sm_evt.id = EVT_CMD_OPEN;
                break;

            case 3: /* Endschalter – wird von sns_limit behandelt */
                break;

            default:
                break;
        }

        if (sm_evt.id != EVT_NONE) {
            sm_sys_post_event(&sm_evt);
        }
    }
}
