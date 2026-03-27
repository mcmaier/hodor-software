/**
 * @file comm_wifi.c
 * @brief WiFi-Task – STA-Modus mit AP-Fallback (Captive Portal).
 *
 * Ablauf:
 *   1. NVS nach wifi_ssid / wifi_pass prüfen
 *   2. Wenn vorhanden → STA-Modus, Verbindung aufbauen
 *   3. Wenn fehlend oder HODOR_WIFI_RETRY_MAX Versuche fehlschlagen → AP-Modus
 *   4. Im AP-Modus: Captive Portal via comm_http, Credentials speichern, Neustart
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
#include "nvs.h"
#include <string.h>

static const char *TAG = "comm_wifi";

/* Bits der comm_event_group */
#define WIFI_CONNECTED_BIT    (1 << 0)
#define WIFI_DISCONNECTED_BIT (1 << 1)
#define MQTT_CONNECTED_BIT    (1 << 2)
#define MQTT_DISCONNECTED_BIT (1 << 3)
#define AP_MODE_ACTIVE_BIT    (1 << 4)

static EventGroupHandle_t s_comm_eg     = NULL;
static int                s_retry_count = 0;
static bool               s_ap_mode     = false;

EventGroupHandle_t comm_get_event_group(void)
{
    return s_comm_eg;
}

/* =========================================================================
 * NVS – WiFi-Credentials laden / speichern
 * ========================================================================= */
static bool load_wifi_credentials(char *ssid, size_t ssid_len,
                                   char *pass, size_t pass_len)
{
    nvs_handle_t nvs;
    if (nvs_open(HODOR_NVS_NAMESPACE, NVS_READONLY, &nvs) != ESP_OK) return false;

    esp_err_t r1 = nvs_get_str(nvs, HODOR_NVS_WIFI_SSID, ssid, &ssid_len);
    esp_err_t r2 = nvs_get_str(nvs, HODOR_NVS_WIFI_PASS, pass, &pass_len);
    nvs_close(nvs);

    if (r1 != ESP_OK || ssid[0] == '\0') return false;
    if (r2 != ESP_OK) pass[0] = '\0'; /* offenes Netzwerk */
    return true;
}

esp_err_t comm_wifi_save_credentials(const char *ssid, const char *pass)
{
    nvs_handle_t nvs;
    esp_err_t ret = nvs_open(HODOR_NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (ret != ESP_OK) return ret;

    ret = nvs_set_str(nvs, HODOR_NVS_WIFI_SSID, ssid);
    if (ret != ESP_OK) { nvs_close(nvs); return ret; }

    ret = nvs_set_str(nvs, HODOR_NVS_WIFI_PASS, pass ? pass : "");
    if (ret != ESP_OK) { nvs_close(nvs); return ret; }

    ret = nvs_commit(nvs);
    nvs_close(nvs);
    return ret;
}

/* =========================================================================
 * Event-Handler
 * ========================================================================= */
static void wifi_event_handler(void *arg, esp_event_base_t base,
                                int32_t id, void *data)
{
    if (base == WIFI_EVENT) {
        switch (id) {
            case WIFI_EVENT_STA_DISCONNECTED:
                xEventGroupClearBits(s_comm_eg, WIFI_CONNECTED_BIT);
                xEventGroupSetBits(s_comm_eg, WIFI_DISCONNECTED_BIT);
                hal_gpio_set(HODOR_GPIO_LED_CONN, 0);

                s_retry_count++;
                if (s_retry_count < HODOR_WIFI_RETRY_MAX) {
                    ESP_LOGW(TAG, "WiFi getrennt – Versuch %d/%d",
                             s_retry_count, HODOR_WIFI_RETRY_MAX);
                    esp_wifi_connect();
                } else {
                    ESP_LOGE(TAG, "WiFi-Verbindung fehlgeschlagen nach %d Versuchen",
                             HODOR_WIFI_RETRY_MAX);
                    /* Fallback wird in comm_wifi_task_func behandelt */
                }
                break;

            case WIFI_EVENT_AP_STACONNECTED: {
                wifi_event_ap_staconnected_t *evt = (wifi_event_ap_staconnected_t *)data;
                ESP_LOGI(TAG, "AP: Client verbunden (AID=%d)", evt->aid);
                break;
            }

            case WIFI_EVENT_AP_STADISCONNECTED: {
                wifi_event_ap_stadisconnected_t *evt = (wifi_event_ap_stadisconnected_t *)data;
                ESP_LOGI(TAG, "AP: Client getrennt (AID=%d)", evt->aid);
                break;
            }

            default: break;
        }
    }
}

static void ip_event_handler(void *arg, esp_event_base_t base,
                              int32_t id, void *data)
{
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *evt = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "IP: " IPSTR, IP2STR(&evt->ip_info.ip));
        s_retry_count = 0;
        xEventGroupClearBits(s_comm_eg, WIFI_DISCONNECTED_BIT | AP_MODE_ACTIVE_BIT);
        xEventGroupSetBits(s_comm_eg, WIFI_CONNECTED_BIT);
        hal_gpio_set(HODOR_GPIO_LED_CONN, 1);
    }
}

/* =========================================================================
 * AP-Modus starten
 * ========================================================================= */
static void start_ap_mode(void)
{
    ESP_LOGI(TAG, "AP-Modus starten: SSID \"%s\"", HODOR_WIFI_AP_SSID);

    esp_netif_create_default_wifi_ap();

    wifi_config_t ap_cfg = {
        .ap = {
            .ssid           = HODOR_WIFI_AP_SSID,
            .ssid_len       = strlen(HODOR_WIFI_AP_SSID),
            .channel        = 1,
            .authmode       = WIFI_AUTH_OPEN,
            .max_connection = 4,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    s_ap_mode = true;
    xEventGroupSetBits(s_comm_eg, AP_MODE_ACTIVE_BIT);
    /* WIFI_CONNECTED_BIT wird NICHT gesetzt – MQTT bleibt inaktiv.
     * HTTP-Server startet via AP_MODE_ACTIVE_BIT. */

    ESP_LOGI(TAG, "AP-Modus aktiv (IP: %s)", HODOR_WIFI_AP_IP);
}

/* =========================================================================
 * STA-Modus starten
 * ========================================================================= */
static bool start_sta_mode(const char *ssid, const char *pass)
{
    ESP_LOGI(TAG, "STA-Modus: Verbinde mit \"%s\"", ssid);

    wifi_config_t sta_cfg = {0};
    strncpy((char *)sta_cfg.sta.ssid,     ssid, sizeof(sta_cfg.sta.ssid) - 1);
    strncpy((char *)sta_cfg.sta.password,  pass, sizeof(sta_cfg.sta.password) - 1);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    s_retry_count = 0;
    s_ap_mode = false;
    esp_wifi_connect();
    return true;
}

/* =========================================================================
 * Init / Task
 * ========================================================================= */
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

    char ssid[33] = {0};
    char pass[65] = {0};

    if (load_wifi_credentials(ssid, sizeof(ssid), pass, sizeof(pass))) {
        /* Gespeicherte Credentials → STA-Modus */
        start_sta_mode(ssid, pass);

        /* Auf Verbindung oder Retry-Limit warten */
        EventBits_t bits = xEventGroupWaitBits(s_comm_eg,
            WIFI_CONNECTED_BIT | WIFI_DISCONNECTED_BIT,
            pdFALSE, pdFALSE, pdMS_TO_TICKS(30000));

        if (!(bits & WIFI_CONNECTED_BIT) && s_retry_count >= HODOR_WIFI_RETRY_MAX) {
            ESP_LOGW(TAG, "STA-Verbindung fehlgeschlagen → AP-Fallback");
            esp_wifi_stop();
            start_ap_mode();
        }
    } else {
        /* Keine Credentials → direkt AP-Modus */
        ESP_LOGI(TAG, "Keine WiFi-Credentials gespeichert");
        start_ap_mode();
    }

    /* Haupt-Loop: LED blinken im AP-Modus, reconnect überwachen */
    for (;;) {
        if (s_ap_mode) {
            /* LED blinken: AP-Modus sichtbar machen */
            hal_gpio_set(HODOR_GPIO_LED_CONN, 1);
            vTaskDelay(pdMS_TO_TICKS(500));
            hal_gpio_set(HODOR_GPIO_LED_CONN, 0);
            vTaskDelay(pdMS_TO_TICKS(500));
        } else {
            vTaskDelay(pdMS_TO_TICKS(5000));

            /* STA-Modus: bei Disconnect nach Retry-Limit → AP-Fallback */
            if (s_retry_count >= HODOR_WIFI_RETRY_MAX) {
                ESP_LOGW(TAG, "Reconnect gescheitert → AP-Fallback");
                esp_wifi_stop();
                start_ap_mode();
            }
        }
    }
}

bool comm_wifi_is_ap_mode(void)
{
    return s_ap_mode;
}
