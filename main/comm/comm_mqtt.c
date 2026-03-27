/**
 * @file comm_mqtt.c
 * @brief MQTT-Client-Task.
 *
 * Befehlstopic:   hodor/cmd   (JSON: {"cmd": "open"} / "close" / "stop" / "toggle")
 * Statustopic:    hodor/status (JSON: {"sys":3,"door":2,"pos":400.0,"i":1.2})
 *
 * Wichtig: Befehle werden in ALLEN Systemzuständen (inkl. SYS_ACTIVE) verarbeitet,
 * da sm_event_queue immer konsumiert wird.
 */

#include "comm_mqtt.h"
#include "comm_wifi.h"
#include "sm_system.h"
#include "hodor_config.h"
#include "hodor_param.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "mqtt_client.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "comm_mqtt";

#define MQTT_TOPIC_CMD    "hodor/cmd"
#define MQTT_TOPIC_STATUS "hodor/status"

static esp_mqtt_client_handle_t s_client = NULL;

/* Status-Queue wird von sm_system.c erstellt und hier verwendet */
#define WIFI_CONNECTED_BIT (1 << 0)

static void parse_and_dispatch(const char *data, int len)
{
    /* Minimaler JSON-Parser: sucht nach "cmd":"open" etc. */
    char buf[64];
    int  copy_len = (len < 63) ? len : 63;
    memcpy(buf, data, copy_len);
    buf[copy_len] = '\0';

    sm_event_t evt = { .id = EVT_NONE, .data = 0 };

    if (strstr(buf, "\"open\""))    evt.id = EVT_CMD_OPEN;
    else if (strstr(buf, "\"close\""))  evt.id = EVT_CMD_CLOSE;
    else if (strstr(buf, "\"stop\""))   evt.id = EVT_CMD_STOP;
    else if (strstr(buf, "\"toggle\"")) evt.id = EVT_CMD_TOGGLE;
    else if (strstr(buf, "\"estop\""))  evt.id = EVT_EMERGENCY_STOP;
    else if (strstr(buf, "\"clear\""))  evt.id = EVT_ERROR_CLEAR;

    if (evt.id != EVT_NONE) {
        sm_sys_post_event(&evt);
    } else {
        ESP_LOGW(TAG, "Unbekannter MQTT-Befehl: %s", buf);
    }
}

static void mqtt_event_handler(void *arg, esp_event_base_t base,
                                int32_t id, void *data)
{
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)data;
    switch (id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT verbunden");
            esp_mqtt_client_subscribe(s_client, MQTT_TOPIC_CMD, 1);
            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "MQTT getrennt");
            break;
        case MQTT_EVENT_DATA:
            if (event->topic_len > 0) {
                parse_and_dispatch(event->data, event->data_len);
            }
            break;
        default: break;
    }
}

esp_err_t comm_mqtt_init(void)
{
    ESP_LOGI(TAG, "MQTT-Task initialisiert");
    return ESP_OK;
}

void comm_mqtt_task_func(void *arg)
{
    (void)arg;

    /* Auf WiFi-Verbindung warten */
    EventGroupHandle_t eg = comm_get_event_group();
    xEventGroupWaitBits(eg, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE, portMAX_DELAY);

    /* MQTT-Client konfigurieren */
    /* TODO: Broker-URL aus NVS laden */
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = "mqtt://192.168.1.1",  /* Platzhalter */
    };
    s_client = esp_mqtt_client_init(&mqtt_cfg);
    if (!s_client) {
        ESP_LOGE(TAG, "esp_mqtt_client_init fehlgeschlagen");
        vTaskDelete(NULL);
        return;
    }
    esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID,
                                   mqtt_event_handler, NULL);
    esp_mqtt_client_start(s_client);

    /* Status aus comm_status_queue publizieren */
    QueueHandle_t status_q = (QueueHandle_t)sm_sys_get_status_queue();
    comm_status_t st;
    char json[128];

    for (;;) {
        if (xQueueReceive(status_q, &st, pdMS_TO_TICKS(1000)) == pdTRUE) {
            snprintf(json, sizeof(json),
                     "{\"sys\":%d,\"door\":%d,\"pos\":%.1f,\"i\":%.2f}",
                     (int)st.sys_state, (int)st.door_state,
                     st.position_mm, st.current_a);
            if (s_client) {
                esp_mqtt_client_publish(s_client, MQTT_TOPIC_STATUS,
                                        json, 0, 0, 0);
            }
        }
    }
}

esp_err_t comm_mqtt_post_status(const comm_status_t *st)
{
    QueueHandle_t q = (QueueHandle_t)sm_sys_get_status_queue();
    if (!q) return ESP_ERR_INVALID_STATE;
    xQueueSend(q, st, 0);
    return ESP_OK;
}
