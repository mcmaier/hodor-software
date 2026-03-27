/**
 * @file main.c
 * @brief HODOR – Hold/Open Door Operation Regulator
 *        App-Einstieg und FreeRTOS-Task-Erstellung.
 *
 * Startup-Sequenz:
 *
 *   Phase 1 – NVS / Parameter (keine Hardware)
 *   Phase 2 – HAL (GPIO, LEDC, ADC, PCNT, SPI) – ESP_ERROR_CHECK, Panic bei Fehler
 *   Phase 3 – Module (Logik-Layer, noch keine Tasks)
 *   Phase 4 – Core-1-Tasks erstellen (Prio aufsteigend)
 *   Phase 5 – Warten auf Hardware-Verifikation (init_done_sem)
 *   Phase 6 – Regler-Timer starten (erst nach Verifikation)
 *   Phase 7 – Core-0-Tasks erstellen (comm)
 *
 * Invarianten:
 *   - Kein MQTT/HTTP-Befehl kann verarbeitet werden bevor sm_task
 *     in SYS_STANDBY ist (Core-0-Tasks starten nach Phase 5)
 *   - Hardware-Watchdog wird NUR aktiviert wenn Motor fahren soll
 *     (mot_wdg_enable() in sm_system.c bei SYS_ACTIVE-Entry)
 *   - MQTT-Befehle werden in ALLEN Zuständen (inkl. ACTIVE) verarbeitet
 *
 * Prioritäten Core 1: ctrl(22) > wdg(21) > sns(20) > sm(18) > door(17) > io(16)
 * Prioritäten Core 0: wifi(12) > mqtt(11) > http(10) > uart(9) > cfg(8)
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "nvs_flash.h"

/* Konfiguration */
#include "hodor_config.h"

/* HAL */
#include "hal_gpio.h"
#include "hal_ledc.h"
#include "hal_adc.h"
#include "hal_pcnt.h"
#include "hal_spi.h"

/* Config / Parameter */
#include "cfg_nvs.h"
#include "hodor_param.h"

/* Motor */
#include "mot_driver.h"
#include "mot_watchdog.h"

/* Sensing */
#include "sns_task.h"

/* I/O */
#include "io_input.h"
#include "io_relay.h"

/* Regler */
#include "ctrl_loop.h"

/* State Machine */
#include "sm_system.h"
#include "sm_door.h"

/* Kommunikation */
#include "comm_wifi.h"
#include "comm_mqtt.h"
#include "comm_http.h"
#include "comm_uart.h"

static const char *TAG = "main";

// Event Group für Task-Synchronisation
EventGroupHandle_t system_event_group;

/* =========================================================================
 * Phase 2: HAL-Init – ESP_ERROR_CHECK (Panic bei Fehler)
 * ========================================================================= */
static void phase2_hal_init(void)
{
    ESP_LOGI(TAG, "Phase 2: HAL-Init");
    ESP_ERROR_CHECK(hal_gpio_init_all());
    ESP_ERROR_CHECK(hal_ledc_init());
    ESP_ERROR_CHECK(hal_adc_init());
    ESP_ERROR_CHECK(hal_pcnt_init());
    ESP_ERROR_CHECK(hal_spi_init());
}

/* =========================================================================
 * Phase 3: Modul-Init
 * Fehler werden gesammelt und über den sm_task-Parameter weitergegeben.
 * Tasks werden trotzdem gestartet, damit mot_wdg_task laufen kann.
 * sm_task erkennt module_err != ESP_OK und schickt EVT_INIT_FAIL.
 * ========================================================================= */
static esp_err_t phase3_module_init(void)
{
    esp_err_t ret = ESP_OK;

#define CHECK_INIT(fn, name)  do {                                         \
    esp_err_t _r = (fn);                                                   \
    if (_r != ESP_OK) {                                                    \
        ESP_LOGE(TAG, name " fehlgeschlagen: %s", esp_err_to_name(_r));    \
        ret = _r;                                                          \
    }                                                                      \
} while(0)

    CHECK_INIT(mot_driver_init(),  "mot_driver_init");
    /* Watchdog nach Motor-Driver – /CLR = LOW (sicher, kein Trigger) */
    CHECK_INIT(mot_wdg_init(),     "mot_wdg_init");
    CHECK_INIT(sns_task_init(),    "sns_task_init");
    CHECK_INIT(io_input_init(),    "io_input_init");
    CHECK_INIT(io_relay_init(),    "io_relay_init");
    CHECK_INIT(ctrl_loop_init(),   "ctrl_loop_init");
    CHECK_INIT(sm_door_init(),     "sm_door_init");
    CHECK_INIT(sm_sys_init(),      "sm_sys_init");
    CHECK_INIT(comm_wifi_init(),   "comm_wifi_init");
    CHECK_INIT(comm_mqtt_init(),   "comm_mqtt_init");
    CHECK_INIT(comm_http_init(),   "comm_http_init");
    CHECK_INIT(comm_uart_init(),   "comm_uart_init");

#undef CHECK_INIT

    return ret;
}

/* =========================================================================
 * app_main
 * ========================================================================= */
void app_main(void)
{
    ESP_LOGI(TAG, "=== HODOR startet ===");

    /* ── Phase 1: NVS / Parameter ─────────────────────────────────────── */
    ESP_LOGI(TAG, "Phase 1: NVS / Parameter");
    ESP_ERROR_CHECK(cfg_nvs_init());
    ESP_ERROR_CHECK(param_init());

    /* ── Phase 2: HAL ─────────────────────────────────────────────────── */
    phase2_hal_init();

    /* ── Phase 3: Module ──────────────────────────────────────────────── */
    ESP_LOGI(TAG, "Phase 3: Module");
    esp_err_t module_err = phase3_module_init();
    /* Fehler werden in sm_task via EVT_INIT_FAIL behandelt;
     * hier nicht abbrechen damit WDG-Task anlaufen kann. */

    /* ── Phase 4: Core-1-Tasks (Prio aufsteigend: niedrigste zuerst) ─── */
    ESP_LOGI(TAG, "Phase 4: Core-1-Tasks");

    xTaskCreatePinnedToCore(io_task_func,        "io_task",
                            HODOR_STACK_IO_TASK,     NULL,
                            HODOR_PRIO_IO_TASK,      NULL,
                            HODOR_CORE_CTRL);

    xTaskCreatePinnedToCore(door_task_func,      "door_task",
                            HODOR_STACK_DOOR_TASK,   NULL,
                            HODOR_PRIO_DOOR_TASK,    NULL,
                            HODOR_CORE_CTRL);

    /* sm_task erhält module_err als Parameter für EVT_INIT_OK/FAIL-Entscheidung */
    xTaskCreatePinnedToCore(sm_task_func,        "sm_task",
                            HODOR_STACK_SM_TASK,     (void *)(intptr_t)module_err,
                            HODOR_PRIO_SM_TASK,      NULL,
                            HODOR_CORE_CTRL);

    xTaskCreatePinnedToCore(sns_task_func,       "sns_task",
                            HODOR_STACK_SNS_TASK,    NULL,
                            HODOR_PRIO_SNS_TASK,     NULL,
                            HODOR_CORE_CTRL);

    /* mot_wdg_task als letzter vor ctrl_task – ab jetzt WDG gesichert */
    xTaskCreatePinnedToCore(mot_wdg_task_func,   "mot_wdg",
                            HODOR_STACK_MOT_WDG_TASK, NULL,
                            HODOR_PRIO_MOT_WDG_TASK,  NULL,
                            HODOR_CORE_CTRL);
    ESP_LOGI(TAG, "mot_wdg_task läuft (inaktiv bis SYS_ACTIVE)");

    /* ctrl_task blockiert auf ctrl_timer_sem – Timer noch nicht gestartet */
    xTaskCreatePinnedToCore(ctrl_task_func,      "ctrl_task",
                            HODOR_STACK_CTRL_TASK,   NULL,
                            HODOR_PRIO_CTRL_TASK,    NULL,
                            HODOR_CORE_CTRL);

    /* ── Phase 5: Warten auf Hardware-Verifikation ────────────────────── */
    ESP_LOGI(TAG, "Phase 5: Warte auf Hardware-Verifikation...");
    SemaphoreHandle_t init_sem = (SemaphoreHandle_t)sm_sys_get_init_sem();
    if (xSemaphoreTake(init_sem, pdMS_TO_TICKS(HODOR_INIT_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGE(TAG, "Init-Timeout nach %u ms – Neustart", HODOR_INIT_TIMEOUT_MS);
        esp_restart();
        return;
    }
    ESP_LOGI(TAG, "Init abgeschlossen – System in STANDBY");

    /* ── Phase 6: Regler-Timer starten ───────────────────────────────── */
    /* ctrl_task ist bereit (blockiert auf Semaphor). Ab jetzt läuft der
     * Regelkreis. Motor bleibt disabled bis sm_task SYS_ACTIVE betritt. */
    ESP_LOGI(TAG, "Phase 6: Regler-Timer gestartet (%u µs)", HODOR_CTRL_PERIOD_US);
    /* TODO: ctrl_loop_start_timer() – der Timer-Handle muss aus ctrl_loop_init()
     * exportiert werden. Interim-Lösung: Timer wird in ctrl_loop_init() gestartet. */

    /* ── Phase 7: Core-0-Tasks ────────────────────────────────────────── */
    ESP_LOGI(TAG, "Phase 7: Core-0-Tasks");

    xTaskCreatePinnedToCore(cfg_task_func,           "cfg_task",
                            HODOR_STACK_CFG_TASK,        NULL,
                            HODOR_PRIO_CFG_TASK,         NULL,
                            HODOR_CORE_COMM);

    xTaskCreatePinnedToCore(comm_uart_task_func,     "uart_task",
                            HODOR_STACK_COMM_UART_TASK,  NULL,
                            HODOR_PRIO_COMM_UART_TASK,   NULL,
                            HODOR_CORE_COMM);

    xTaskCreatePinnedToCore(comm_wifi_task_func,     "wifi_task",
                            HODOR_STACK_COMM_WIFI_TASK,  NULL,
                            HODOR_PRIO_COMM_WIFI_TASK,   NULL,
                            HODOR_CORE_COMM);

    xTaskCreatePinnedToCore(comm_mqtt_task_func,     "mqtt_task",
                            HODOR_STACK_COMM_MQTT_TASK,  NULL,
                            HODOR_PRIO_COMM_MQTT_TASK,   NULL,
                            HODOR_CORE_COMM);

    xTaskCreatePinnedToCore(comm_http_task_func,     "http_task",
                            HODOR_STACK_COMM_HTTP_TASK,  NULL,
                            HODOR_PRIO_COMM_HTTP_TASK,   NULL,
                            HODOR_CORE_COMM);

    ESP_LOGI(TAG, "Alle Tasks erstellt – FreeRTOS-Scheduler übernimmt");
    /* app_main gibt Stack zurück; Scheduler verwaltet alle Tasks */
}
