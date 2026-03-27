/**
 * @file comm_http.c
 * @brief HTTP-Webserver – Stub mit URI-Handler-Skelett.
 *
 * REST-Endpunkte (Implementierung in docs/webserver.md spezifiziert):
 *   GET  /api/status       → JSON Systemstatus
 *   GET  /api/params       → Alle Parameter auflisten
 *   POST /api/params/{id}  → Parameter setzen
 *   POST /api/cmd          → Befehl senden (open/close/stop)
 */

#include "comm_http.h"
#include "comm_wifi.h"
#include "hodor_config.h"
#include "sm_system.h"
#include "sns_task.h"
#include "hodor_param.h"
#include "cfg_nvs.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "comm_http";

#define WIFI_CONNECTED_BIT (1 << 0)

static httpd_handle_t s_server = NULL;

/* ── GET /api/status ─────────────────────────────────────────────────────── */
static esp_err_t handle_status(httpd_req_t *req)
{
    sns_data_t telem;
    sns_get_telemetry(&telem);
    char json[200];
    snprintf(json, sizeof(json),
             "{\"sys\":%d,\"door\":%d,\"pos\":%.1f,\"vel\":%.1f,\"i\":%.3f}",
             (int)sm_sys_get_state(), (int)0 /* door state TODO */,
             telem.position_mm, telem.velocity_mms, telem.current_a);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, json);
}

/* ── POST /api/cmd ───────────────────────────────────────────────────────── */
static esp_err_t handle_cmd(httpd_req_t *req)
{
    char buf[64];
    int  len = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (len <= 0) return ESP_FAIL;
    buf[len] = '\0';

    sm_event_t evt = { .id = EVT_NONE, .data = 0 };
    if      (strstr(buf, "open"))   evt.id = EVT_CMD_OPEN;
    else if (strstr(buf, "close"))  evt.id = EVT_CMD_CLOSE;
    else if (strstr(buf, "stop"))   evt.id = EVT_CMD_STOP;
    else if (strstr(buf, "toggle")) evt.id = EVT_CMD_TOGGLE;

    if (evt.id != EVT_NONE) {
        sm_sys_post_event(&evt);
        return httpd_resp_sendstr(req, "{\"ok\":true}");
    }
    httpd_resp_set_status(req, "400 Bad Request");
    return httpd_resp_sendstr(req, "{\"ok\":false}");
}

/* ── GET /api/params ─────────────────────────────────────────────────────── */
static esp_err_t handle_params_list(httpd_req_t *req)
{
    /* TODO: vollständige Parameterliste als JSON – Stub */
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"params\":[]}");
}

static const httpd_uri_t uri_status = {
    .uri = "/api/status", .method = HTTP_GET, .handler = handle_status };
static const httpd_uri_t uri_cmd = {
    .uri = "/api/cmd",    .method = HTTP_POST, .handler = handle_cmd };
static const httpd_uri_t uri_params = {
    .uri = "/api/params", .method = HTTP_GET, .handler = handle_params_list };

esp_err_t comm_http_init(void)
{
    ESP_LOGI(TAG, "HTTP-Server-Stub initialisiert");
    return ESP_OK;
}

void comm_http_task_func(void *arg)
{
    (void)arg;

    EventGroupHandle_t eg = comm_get_event_group();
    xEventGroupWaitBits(eg, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE, portMAX_DELAY);

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.task_priority  = HODOR_PRIO_COMM_HTTP_TASK;
    cfg.stack_size     = HODOR_STACK_COMM_HTTP_TASK * sizeof(StackType_t);
    cfg.core_id        = HODOR_CORE_COMM;

    if (httpd_start(&s_server, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start fehlgeschlagen");
        vTaskDelete(NULL);
        return;
    }

    httpd_register_uri_handler(s_server, &uri_status);
    httpd_register_uri_handler(s_server, &uri_cmd);
    httpd_register_uri_handler(s_server, &uri_params);

    ESP_LOGI(TAG, "HTTP-Server gestartet");

    /* HTTP-Server läuft intern – Task wartet auf Event-Group */
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
