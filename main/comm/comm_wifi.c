/**
 * @file comm_wifi.c
 * @brief WiFi-Task – STA-Modus mit AP-Fallback (Captive Portal).
 */

#include "comm_wifi.h"
#include "hodor_config.h"
#include "hal_gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "nvs_flash.h"

static const char *TAG = "comm_wifi";

/* Bits der comm_event_group */
#define WIFI_CONNECTED_BIT    (1 << 0)
#define WIFI_DISCONNECTED_BIT (1 << 1)
#define MQTT_CONNECTED_BIT    (1 << 2)
#define MQTT_DISCONNECTED_BIT (1 << 3)
#define AP_MODE_ACTIVE_BIT    (1 << 4)

static EventGroupHandle_t s_comm_eg = NULL;

/* Accessor für andere comm-Module */
EventGroupHandle_t comm_get_event_group(void)
{
    return s_comm_eg;
}

static void wifi_event_handler(void *arg, esp_event_base_t base,
                                int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(s_comm_eg, WIFI_CONNECTED_BIT);
        xEventGroupSetBits(s_comm_eg, WIFI_DISCONNECTED_BIT);
        hal_gpio_set(HODOR_GPIO_LED_CONN, 0);
        ESP_LOGW(TAG, "WiFi getrennt – reconnect...");
        esp_wifi_connect();
    }
}

static void ip_event_handler(void *arg, esp_event_base_t base,
                              int32_t id, void *data)
{
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        xEventGroupClearBits(s_comm_eg, WIFI_DISCONNECTED_BIT);
        xEventGroupSetBits(s_comm_eg, WIFI_CONNECTED_BIT);
        ESP_LOGI(TAG, "WiFi verbunden");
    }
}

esp_err_t comm_wifi_init(void)
{
    s_comm_eg = xEventGroupCreate();
    if (!s_comm_eg) return ESP_ERR_NO_MEM;

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                               wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                               ip_event_handler, NULL));

    ESP_LOGI(TAG, "WiFi initialisiert");
    return ESP_OK;
}

void comm_wifi_task_func(void *arg)
{
    (void)arg;

    /* TODO: SSID/Passwort aus NVS laden */
    wifi_config_t wifi_cfg = {
        .sta = {
            .ssid     = "HODOR_AP",   /* Platzhalter */
            .password = "",
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());
    esp_wifi_connect();

    for (;;) {
        /* Reconnect-Logik wird im Event-Handler behandelt */
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
