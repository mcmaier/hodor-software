/**
 * @file comm_http.c
 * @brief HTTP-Webserver – REST-API und Captive-Portal.
 *
 * REST-API (docs/webserver.md §6):
 *   GET  /api/status            → Systemstatus JSON
 *   POST /api/cmd               → Steuerbefehl
 *   GET  /api/param/list        → Alle Parameter
 *   POST /api/param/set         → Parameter setzen
 *   POST /api/param/reset       → Alle auf Default
 *   GET  /api/wifi/status       → WiFi-Status
 *   POST /api/wifi/save         → WiFi-Credentials speichern + Neustart
 *   POST /api/system/restart    → Neustart
 *   POST /api/system/factory_reset → Werksreset + Neustart
 *
 * Captive-Portal (AP-Modus):
 *   GET /generate_204, /hotspot-detect.html, /ncsi.txt → 302 → /
 *   GET / → WLAN-Setup-Seite (minimales HTML)
 */

#include "comm_http.h"
#include "comm_wifi.h"
#include "comm_mqtt.h"
#include "sm_system.h"
#include "sm_door.h"
#include "sns_task.h"
#include "hodor_config.h"
#include "hodor_param.h"
#include "cfg_nvs.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

static const char *TAG = "comm_http";

#define WIFI_CONNECTED_BIT (1 << 0)
#define AP_MODE_ACTIVE_BIT (1 << 4)

static httpd_handle_t s_server = NULL;

/* Hilfs-Makro: JSON-Response senden */
#define SEND_JSON(req, json) do {                     \
    httpd_resp_set_type((req), "application/json");   \
    httpd_resp_sendstr((req), (json));                \
} while (0)

/* Hilfs-Makro: Fehlende Body-Daten */
#define RECV_BODY(req, buf, maxlen) do {                      \
    int _len = httpd_req_recv((req), (buf), (maxlen) - 1);    \
    if (_len <= 0) {                                           \
        SEND_JSON((req), "{\"ok\":false,\"err\":\"no_body\"}"); \
        return ESP_OK;                                          \
    }                                                           \
    (buf)[_len] = '\0';                                         \
} while (0)

/* =========================================================================
 * Einfacher JSON-Wert-Extraktor (kein vollständiger Parser)
 * Sucht nach "key":value und gibt den Value-Start zurück.
 * ========================================================================= */
static const char *json_find_key(const char *json, const char *key)
{
    char pattern[32];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *p = strstr(json, pattern);
    if (!p) return NULL;
    p += strlen(pattern);
    while (*p == ' ' || *p == ':') p++;
    return p;
}

static bool json_get_string(const char *json, const char *key, char *out, size_t maxlen)
{
    const char *p = json_find_key(json, key);
    if (!p || *p != '"') return false;
    p++; /* skip opening quote */
    size_t i = 0;
    while (*p && *p != '"' && i < maxlen - 1) {
        out[i++] = *p++;
    }
    out[i] = '\0';
    return i > 0;
}

static bool json_get_float(const char *json, const char *key, float *out)
{
    const char *p = json_find_key(json, key);
    if (!p) return false;
    char *end = NULL;
    *out = strtof(p, &end);
    return end != p;
}

static bool json_get_uint(const char *json, const char *key, uint32_t *out)
{
    const char *p = json_find_key(json, key);
    if (!p) return false;
    char *end = NULL;
    unsigned long v = strtoul(p, &end, 0);  /* 0 = auto-detect hex/dec */
    *out = (uint32_t)v;
    return end != p;
}

/* =========================================================================
 * GET /api/status
 * ========================================================================= */
static esp_err_t handle_status(httpd_req_t *req)
{
    sns_data_t telem;
    sns_get_telemetry(&telem);
    char json[256];
    snprintf(json, sizeof(json),
             "{\"sys\":%d,\"door\":%d,"
             "\"pos\":%.1f,\"vel\":%.1f,\"i\":%.3f,"
             "\"ap_mode\":%s}",
             (int)sm_sys_get_state(), (int)sm_door_get_state(),
             telem.position_mm, telem.velocity_mms, telem.current_a,
             comm_wifi_is_ap_mode() ? "true" : "false");
    SEND_JSON(req, json);
    return ESP_OK;
}

/* =========================================================================
 * POST /api/cmd
 * Body: {"cmd":"open"} / "close" / "stop" / "toggle" / "estop" / "clear"
 * ========================================================================= */
static esp_err_t handle_cmd(httpd_req_t *req)
{
    char buf[64];
    RECV_BODY(req, buf, sizeof(buf));

    sm_event_t evt = { .id = EVT_NONE, .data = 0 };
    if      (strstr(buf, "\"open\""))   evt.id = EVT_CMD_OPEN;
    else if (strstr(buf, "\"close\""))  evt.id = EVT_CMD_CLOSE;
    else if (strstr(buf, "\"stop\""))   evt.id = EVT_CMD_STOP;
    else if (strstr(buf, "\"toggle\"")) evt.id = EVT_CMD_TOGGLE;
    else if (strstr(buf, "\"estop\""))  evt.id = EVT_EMERGENCY_STOP;
    else if (strstr(buf, "\"clear\""))  evt.id = EVT_ERROR_CLEAR;

    if (evt.id != EVT_NONE) {
        sm_sys_post_event(&evt);
        SEND_JSON(req, "{\"ok\":true}");
    } else {
        httpd_resp_set_status(req, "400 Bad Request");
        SEND_JSON(req, "{\"ok\":false,\"err\":\"unknown_cmd\"}");
    }
    return ESP_OK;
}

/* =========================================================================
 * GET /api/param/list
 * ========================================================================= */
static void send_param_json(httpd_req_t *req, const param_desc_t *p)
{
    char entry[192];
    param_value_t v;
    param_get((param_id_t)p->id, &v);

    switch (p->type) {
        case PARAM_TYPE_FLOAT:
            snprintf(entry, sizeof(entry),
                "{\"id\":\"0x%04x\",\"name\":\"%s\",\"type\":\"float\","
                "\"val\":%.6g,\"min\":%.6g,\"max\":%.6g,\"unit\":\"%s\",\"flags\":%u}",
                p->id, p->name, v.f, p->min.f, p->max.f, p->unit, p->flags);
            break;
        case PARAM_TYPE_UINT16:
            snprintf(entry, sizeof(entry),
                "{\"id\":\"0x%04x\",\"name\":\"%s\",\"type\":\"uint16\","
                "\"val\":%u,\"min\":%u,\"max\":%u,\"unit\":\"%s\",\"flags\":%u}",
                p->id, p->name, v.u16, p->min.u16, p->max.u16, p->unit, p->flags);
            break;
        case PARAM_TYPE_UINT8:
        case PARAM_TYPE_BOOL:
            snprintf(entry, sizeof(entry),
                "{\"id\":\"0x%04x\",\"name\":\"%s\",\"type\":\"%s\","
                "\"val\":%u,\"min\":%u,\"max\":%u,\"unit\":\"%s\",\"flags\":%u}",
                p->id, p->name,
                (p->type == PARAM_TYPE_BOOL) ? "bool" : "uint8",
                v.u8, p->min.u8, p->max.u8, p->unit, p->flags);
            break;
    }
    httpd_resp_sendstr_chunk(req, entry);
}

static esp_err_t handle_param_list(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr_chunk(req, "{\"params\":[");

    size_t count = param_count();
    for (size_t i = 0; i < count; i++) {
        if (i > 0) httpd_resp_sendstr_chunk(req, ",");
        const param_desc_t *p = param_get_by_index(i);
        if (p) send_param_json(req, p);
    }

    httpd_resp_sendstr_chunk(req, "]}");
    httpd_resp_sendstr_chunk(req, NULL); /* flush */
    return ESP_OK;
}

/* =========================================================================
 * POST /api/param/set
 * Body: {"id":"0x0201","val":1.5}
 * ========================================================================= */
static esp_err_t handle_param_set(httpd_req_t *req)
{
    char buf[128];
    RECV_BODY(req, buf, sizeof(buf));

    /* ID parsen */
    uint32_t id_raw = 0;
    if (!json_get_uint(buf, "id", &id_raw)) {
        /* Versuche als Hex-String: "id":"0x0201" */
        char id_str[16] = {0};
        if (!json_get_string(buf, "id", id_str, sizeof(id_str))) {
            SEND_JSON(req, "{\"ok\":false,\"err\":\"missing_id\"}");
            return ESP_OK;
        }
        id_raw = (uint32_t)strtoul(id_str, NULL, 0);
    }

    const param_desc_t *p = param_get_desc((param_id_t)id_raw);
    if (!p) {
        SEND_JSON(req, "{\"ok\":false,\"err\":\"unknown_id\"}");
        return ESP_OK;
    }

    /* Wert parsen und setzen */
    param_value_t val = {0};
    char resp[128];

    switch (p->type) {
        case PARAM_TYPE_FLOAT: {
            float fv;
            if (!json_get_float(buf, "val", &fv)) {
                SEND_JSON(req, "{\"ok\":false,\"err\":\"missing_val\"}");
                return ESP_OK;
            }
            val.f = fv;
            break;
        }
        case PARAM_TYPE_UINT16: {
            uint32_t uv;
            if (!json_get_uint(buf, "val", &uv)) {
                SEND_JSON(req, "{\"ok\":false,\"err\":\"missing_val\"}");
                return ESP_OK;
            }
            val.u16 = (uint16_t)uv;
            break;
        }
        case PARAM_TYPE_UINT8:
        case PARAM_TYPE_BOOL: {
            uint32_t uv;
            if (!json_get_uint(buf, "val", &uv)) {
                SEND_JSON(req, "{\"ok\":false,\"err\":\"missing_val\"}");
                return ESP_OK;
            }
            val.u8 = (uint8_t)uv;
            break;
        }
    }

    esp_err_t ret = param_set((param_id_t)id_raw, val);
    if (ret == ESP_ERR_INVALID_ARG) {
        snprintf(resp, sizeof(resp), "{\"ok\":false,\"err\":\"out_of_range\"}");
    } else if (ret == ESP_ERR_NOT_SUPPORTED) {
        snprintf(resp, sizeof(resp), "{\"ok\":false,\"err\":\"readonly\"}");
    } else if (ret != ESP_OK) {
        snprintf(resp, sizeof(resp), "{\"ok\":false,\"err\":\"%s\"}", esp_err_to_name(ret));
    } else {
        /* Erfolg – bei PERSIST-Flag in NVS speichern */
        if (p->flags & PARAM_FLAG_PERSIST) {
            cfg_nvs_queue_save((param_id_t)id_raw);
        }
        snprintf(resp, sizeof(resp), "{\"ok\":true,\"id\":\"0x%04" PRIx32 "\"}", id_raw);
    }
    SEND_JSON(req, resp);
    return ESP_OK;
}

/* =========================================================================
 * POST /api/param/reset
 * ========================================================================= */
static esp_err_t handle_param_reset(httpd_req_t *req)
{
    param_reset_all();
    cfg_nvs_erase_all();
    SEND_JSON(req, "{\"ok\":true}");
    return ESP_OK;
}

/* =========================================================================
 * GET /api/wifi/status
 * ========================================================================= */
static esp_err_t handle_wifi_status(httpd_req_t *req)
{
    char json[128];
    snprintf(json, sizeof(json),
             "{\"ap_mode\":%s,\"connected\":%s}",
             comm_wifi_is_ap_mode() ? "true" : "false",
             comm_wifi_is_ap_mode() ? "false" : "true");
    SEND_JSON(req, json);
    return ESP_OK;
}

/* =========================================================================
 * POST /api/wifi/save
 * Body: {"ssid":"MyNetwork","pass":"secret"}
 * Speichert Credentials und startet Neustart.
 * ========================================================================= */
static esp_err_t handle_wifi_save(httpd_req_t *req)
{
    char buf[160];
    RECV_BODY(req, buf, sizeof(buf));

    char ssid[33] = {0};
    char pass[65] = {0};
    if (!json_get_string(buf, "ssid", ssid, sizeof(ssid))) {
        SEND_JSON(req, "{\"ok\":false,\"err\":\"missing_ssid\"}");
        return ESP_OK;
    }
    json_get_string(buf, "pass", pass, sizeof(pass)); /* optional */

    esp_err_t ret = comm_wifi_save_credentials(ssid, pass);
    if (ret != ESP_OK) {
        char resp[64];
        snprintf(resp, sizeof(resp), "{\"ok\":false,\"err\":\"%s\"}", esp_err_to_name(ret));
        SEND_JSON(req, resp);
        return ESP_OK;
    }

    SEND_JSON(req, "{\"ok\":true,\"msg\":\"restart\"}");

    /* Verzögerter Neustart damit die HTTP-Response noch gesendet wird */
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK;
}

/* =========================================================================
 * POST /api/system/restart
 * ========================================================================= */
static esp_err_t handle_restart(httpd_req_t *req)
{
    SEND_JSON(req, "{\"ok\":true,\"msg\":\"restart\"}");
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK;
}

/* =========================================================================
 * POST /api/system/factory_reset
 * ========================================================================= */
static esp_err_t handle_factory_reset(httpd_req_t *req)
{
    param_reset_all();
    cfg_nvs_erase_all();
    SEND_JSON(req, "{\"ok\":true,\"msg\":\"factory_reset\"}");
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK;
}

/* =========================================================================
 * Captive-Portal: Redirect-Handler (AP-Modus)
 * ========================================================================= */
static esp_err_t handle_captive_redirect(httpd_req_t *req)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://" HODOR_WIFI_AP_IP "/");
    httpd_resp_sendstr(req, "");
    return ESP_OK;
}

/* =========================================================================
 * GET / – Minimal-Seite (AP: WiFi-Setup, STA: Redirect → /api/status)
 * ========================================================================= */
static const char WIFI_SETUP_HTML[] =
    "<!DOCTYPE html><html><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>HODOR Setup</title>"
    "<style>body{font-family:sans-serif;max-width:400px;margin:40px auto;padding:0 20px}"
    "h1{color:#333}input,button{width:100%;padding:10px;margin:6px 0;box-sizing:border-box}"
    "button{background:#2196F3;color:#fff;border:none;cursor:pointer;font-size:16px}"
    "button:hover{background:#1976D2}</style></head><body>"
    "<h1>HODOR WiFi-Setup</h1>"
    "<form method='POST' action='/api/wifi/save'>"
    "<label>SSID</label><input name='ssid' type='text' required>"
    "<label>Passwort</label><input name='pass' type='password'>"
    "<button type='submit'>Verbinden</button>"
    "</form></body></html>";

static esp_err_t handle_root(httpd_req_t *req)
{
    if (comm_wifi_is_ap_mode()) {
        httpd_resp_set_type(req, "text/html");
        httpd_resp_sendstr(req, WIFI_SETUP_HTML);
    } else {
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", "/api/status");
        httpd_resp_sendstr(req, "");
    }
    return ESP_OK;
}

/* =========================================================================
 * URI-Handler-Tabelle
 * ========================================================================= */
static const httpd_uri_t s_uris[] = {
    { .uri = "/",                       .method = HTTP_GET,  .handler = handle_root },
    { .uri = "/api/status",             .method = HTTP_GET,  .handler = handle_status },
    { .uri = "/api/cmd",                .method = HTTP_POST, .handler = handle_cmd },
    { .uri = "/api/param/list",         .method = HTTP_GET,  .handler = handle_param_list },
    { .uri = "/api/param/set",          .method = HTTP_POST, .handler = handle_param_set },
    { .uri = "/api/param/reset",        .method = HTTP_POST, .handler = handle_param_reset },
    { .uri = "/api/wifi/status",        .method = HTTP_GET,  .handler = handle_wifi_status },
    { .uri = "/api/wifi/save",          .method = HTTP_POST, .handler = handle_wifi_save },
    { .uri = "/api/system/restart",     .method = HTTP_POST, .handler = handle_restart },
    { .uri = "/api/system/factory_reset", .method = HTTP_POST, .handler = handle_factory_reset },
    /* Captive-Portal Redirects */
    { .uri = "/generate_204",           .method = HTTP_GET,  .handler = handle_captive_redirect },
    { .uri = "/hotspot-detect.html",    .method = HTTP_GET,  .handler = handle_captive_redirect },
    { .uri = "/ncsi.txt",               .method = HTTP_GET,  .handler = handle_captive_redirect },
};

#define URI_COUNT (sizeof(s_uris) / sizeof(s_uris[0]))

/* =========================================================================
 * Init / Task
 * ========================================================================= */
esp_err_t comm_http_init(void)
{
    ESP_LOGI(TAG, "HTTP-Server initialisiert");
    return ESP_OK;
}

void comm_http_task_func(void *arg)
{
    (void)arg;

    EventGroupHandle_t eg = comm_get_event_group();
    xEventGroupWaitBits(eg, WIFI_CONNECTED_BIT | AP_MODE_ACTIVE_BIT,
                        pdFALSE, pdFALSE, portMAX_DELAY);

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.max_uri_handlers = URI_COUNT + 2;
    cfg.stack_size       = HODOR_STACK_COMM_HTTP_TASK * sizeof(StackType_t);
    cfg.core_id          = HODOR_CORE_COMM;

    if (httpd_start(&s_server, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start fehlgeschlagen");
        vTaskDelete(NULL);
        return;
    }

    for (size_t i = 0; i < URI_COUNT; i++) {
        httpd_register_uri_handler(s_server, &s_uris[i]);
    }

    ESP_LOGI(TAG, "HTTP-Server gestartet (%zu Endpunkte)", URI_COUNT);

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
