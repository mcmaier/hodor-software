/**
 * @file mot_watchdog.c
 * @brief Hardware-Watchdog 74LVC1G123 – Retrigger-Task.
 *
 * Sicherheitslogik:
 *   s_wdg_enabled = false  → kein Trigger → 74LVC1G123 Timeout → H-Brücke aus
 *   s_wdg_enabled = true   → Trigger alle HODOR_WDG_RETRIGGER_MS ms
 *
 * s_wdg_enabled wird nur aus Core 1 gesetzt/gelesen (mot_wdg_task läuft auf
 * Core 1, sm_task ruft enable/disable auf Core 1 auf) → volatile uint32_t
 * reicht für atomaren Zugriff auf Xtensa LX7.
 */

#include "mot_watchdog.h"
#include "hal_gpio.h"
#include "hodor_config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "esp_log.h"

static const char *TAG = "mot_wdg";

static SemaphoreHandle_t   s_wdg_sem    = NULL;
static esp_timer_handle_t  s_timer      = NULL;
static volatile uint32_t   s_wdg_enabled = 0;  /* 0 = off, 1 = on */

static void wdg_timer_cb(void *arg)
{
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(s_wdg_sem, &woken);
    if (woken) portYIELD_FROM_ISR();
}

esp_err_t mot_wdg_init(void)
{
    /* Sicherer Anfangszustand: Watchdog in Reset */
    hal_gpio_set(HODOR_GPIO_WDG_TRIG, 0);
    hal_gpio_set(HODOR_GPIO_WDG_NCLR, 0);  /* /CLR = LOW → Watchdog resetiert */
    s_wdg_enabled = 0;

    s_wdg_sem = xSemaphoreCreateBinary();
    if (!s_wdg_sem) return ESP_ERR_NO_MEM;

    esp_timer_create_args_t timer_args = {
        .callback        = wdg_timer_cb,
        .arg             = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name            = "mot_wdg",
    };
    esp_err_t ret = esp_timer_create(&timer_args, &s_timer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_timer_create: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Timer sofort starten – Task läuft, triggert aber nicht (s_wdg_enabled=0) */
    ret = esp_timer_start_periodic(s_timer, HODOR_WDG_RETRIGGER_MS * 1000ULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_timer_start_periodic: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Hardware-Watchdog initialisiert (inaktiv)");
    return ESP_OK;
}

esp_err_t mot_wdg_enable(void)
{
    /* /CLR HIGH: Watchdog aus Reset nehmen */
    hal_gpio_set(HODOR_GPIO_WDG_NCLR, 1);
    s_wdg_enabled = 1;
    ESP_LOGI(TAG, "Watchdog aktiviert");
    return ESP_OK;
}

void mot_wdg_disable(void)
{
    s_wdg_enabled = 0;
    /* /CLR LOW: sofortiges Hardware-Reset → H-Brücke unverzüglich abgeschaltet */
    hal_gpio_set(HODOR_GPIO_WDG_NCLR, 0);
    hal_gpio_set(HODOR_GPIO_WDG_TRIG, 0);
    ESP_LOGI(TAG, "Watchdog deaktiviert");
}

void mot_wdg_task_func(void *arg)
{
    (void)arg;

    for (;;) {
        xSemaphoreTake(s_wdg_sem, portMAX_DELAY);

        if (s_wdg_enabled) {
            /* Minimaler Puls: HIGH → LOW (74LVC1G123 benötigt nur 10 ns) */
            hal_gpio_set(HODOR_GPIO_WDG_TRIG, 1);
            hal_gpio_set(HODOR_GPIO_WDG_TRIG, 0);
        }
        /* Kein Trigger wenn s_wdg_enabled == 0:
         * 74LVC1G123 Timeout → H-Brücken-Pfad hardwareseitig unterbrochen. */
    }
}
