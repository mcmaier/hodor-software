/**
 * @file comm_uart.c
 * @brief UART-Telemetrie-Stream und JSON-Parameter-Protokoll.
 *
 * Protokoll gemäß docs/parameters.md §7:
 *
 *   {"cmd":"get","id":"0x0201"}         → Parameter lesen
 *   {"cmd":"set","id":"0x0201","val":1.5} → Parameter setzen
 *   {"cmd":"list"}                      → Alle Parameter auflisten
 *   {"cmd":"stream","en":true}          → Telemetrie-Stream starten/stoppen
 *
 * Stream-Format (newline-terminiert):
 *   {"t":1234,"i":1.23,"v":150.5,"p":412.0,"pwm":45.2,"ss":3,"ds":1}
 *
 * Framing: JSON-Zeilen, \n-terminiert, 115200 Baud.
 */

#include "comm_uart.h"
#include "sns_task.h"
#include "sm_system.h"
#include "sm_door.h"
#include "mot_driver.h"
#include "hodor_param.h"
#include "cfg_nvs.h"
#include "hodor_config.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

static const char *TAG = "comm_uart";

#define UART_BUF_SIZE  256
#define TX_BUF_SIZE    192

static bool s_stream_en = false;

/* =========================================================================
 * Einfache JSON-Hilfsfunktionen
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
    p++;
    size_t i = 0;
    while (*p && *p != '"' && i < maxlen - 1) out[i++] = *p++;
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
    *out = (uint32_t)strtoul(p, &end, 0);
    return end != p;
}

/* =========================================================================
 * Einzelnen Parameter als JSON-Zeile ausgeben
 * ========================================================================= */
static void send_param(const param_desc_t *p, param_value_t v)
{
    char line[TX_BUF_SIZE];
    switch (p->type) {
        case PARAM_TYPE_FLOAT:
            snprintf(line, sizeof(line),
                "{\"id\":\"0x%04x\",\"name\":\"%s\",\"val\":%.6g,"
                "\"unit\":\"%s\",\"min\":%.6g,\"max\":%.6g}\n",
                p->id, p->name, v.f, p->unit, p->min.f, p->max.f);
            break;
        case PARAM_TYPE_UINT16:
            snprintf(line, sizeof(line),
                "{\"id\":\"0x%04x\",\"name\":\"%s\",\"val\":%u,"
                "\"unit\":\"%s\",\"min\":%u,\"max\":%u}\n",
                p->id, p->name, v.u16, p->unit, p->min.u16, p->max.u16);
            break;
        case PARAM_TYPE_UINT8:
        case PARAM_TYPE_BOOL:
            snprintf(line, sizeof(line),
                "{\"id\":\"0x%04x\",\"name\":\"%s\",\"val\":%u,"
                "\"unit\":\"%s\",\"min\":%u,\"max\":%u}\n",
                p->id, p->name, v.u8, p->unit, p->min.u8, p->max.u8);
            break;
    }
    printf("%s", line);
}

/* =========================================================================
 * Befehlsverarbeitung
 * ========================================================================= */
static void cmd_get(const char *buf)
{
    char id_str[16] = {0};
    if (!json_get_string(buf, "id", id_str, sizeof(id_str))) {
        printf("{\"ok\":false,\"err\":\"missing_id\"}\n");
        return;
    }
    uint16_t id = (uint16_t)strtoul(id_str, NULL, 0);
    const param_desc_t *p = param_get_desc((param_id_t)id);
    if (!p) {
        printf("{\"ok\":false,\"err\":\"unknown_id\"}\n");
        return;
    }
    param_value_t v;
    param_get((param_id_t)id, &v);
    send_param(p, v);
}

static void cmd_set(const char *buf)
{
    char id_str[16] = {0};
    if (!json_get_string(buf, "id", id_str, sizeof(id_str))) {
        printf("{\"ok\":false,\"err\":\"missing_id\"}\n");
        return;
    }
    uint16_t id = (uint16_t)strtoul(id_str, NULL, 0);
    const param_desc_t *p = param_get_desc((param_id_t)id);
    if (!p) {
        printf("{\"id\":\"%s\",\"ok\":false,\"err\":\"unknown_id\"}\n", id_str);
        return;
    }

    param_value_t val = {0};
    switch (p->type) {
        case PARAM_TYPE_FLOAT: {
            float fv;
            if (!json_get_float(buf, "val", &fv)) {
                printf("{\"id\":\"%s\",\"ok\":false,\"err\":\"missing_val\"}\n", id_str);
                return;
            }
            val.f = fv;
            break;
        }
        case PARAM_TYPE_UINT16: {
            uint32_t uv;
            if (!json_get_uint(buf, "val", &uv)) {
                printf("{\"id\":\"%s\",\"ok\":false,\"err\":\"missing_val\"}\n", id_str);
                return;
            }
            val.u16 = (uint16_t)uv;
            break;
        }
        case PARAM_TYPE_UINT8:
        case PARAM_TYPE_BOOL: {
            uint32_t uv;
            if (!json_get_uint(buf, "val", &uv)) {
                printf("{\"id\":\"%s\",\"ok\":false,\"err\":\"missing_val\"}\n", id_str);
                return;
            }
            val.u8 = (uint8_t)uv;
            break;
        }
    }

    esp_err_t ret = param_set((param_id_t)id, val);
    if (ret == ESP_ERR_INVALID_ARG) {
        printf("{\"id\":\"%s\",\"ok\":false,\"err\":\"out_of_range\"}\n", id_str);
    } else if (ret == ESP_ERR_NOT_SUPPORTED) {
        printf("{\"id\":\"%s\",\"ok\":false,\"err\":\"readonly\"}\n", id_str);
    } else if (ret != ESP_OK) {
        printf("{\"id\":\"%s\",\"ok\":false,\"err\":\"%s\"}\n", id_str, esp_err_to_name(ret));
    } else {
        /* NVS speichern wenn persistenter Parameter */
        if (p->flags & PARAM_FLAG_PERSIST) {
            cfg_nvs_queue_save((param_id_t)id);
        }
        /* Erfolgsantwort mit aktuellem Wert */
        param_value_t readback;
        param_get((param_id_t)id, &readback);
        switch (p->type) {
            case PARAM_TYPE_FLOAT:
                printf("{\"id\":\"%s\",\"ok\":true,\"val\":%.6g}\n", id_str, readback.f);
                break;
            case PARAM_TYPE_UINT16:
                printf("{\"id\":\"%s\",\"ok\":true,\"val\":%u}\n", id_str, readback.u16);
                break;
            case PARAM_TYPE_UINT8:
            case PARAM_TYPE_BOOL:
                printf("{\"id\":\"%s\",\"ok\":true,\"val\":%u}\n", id_str, readback.u8);
                break;
        }
    }
}

static void cmd_list(void)
{
    for (size_t i = 0; i < param_count(); i++) {
        const param_desc_t *p = param_get_by_index(i);
        if (!p) continue;
        param_value_t v;
        param_get((param_id_t)p->id, &v);
        send_param(p, v);
    }
}

static void cmd_stream(const char *buf)
{
    s_stream_en = (strstr(buf, "true") != NULL);
    printf("{\"ok\":true,\"stream\":%s}\n", s_stream_en ? "true" : "false");
}

static void process_json_cmd(const char *buf)
{
    char cmd[16] = {0};
    if (!json_get_string(buf, "cmd", cmd, sizeof(cmd))) return;

    if      (strcmp(cmd, "get")    == 0) cmd_get(buf);
    else if (strcmp(cmd, "set")    == 0) cmd_set(buf);
    else if (strcmp(cmd, "list")   == 0) cmd_list();
    else if (strcmp(cmd, "stream") == 0) cmd_stream(buf);
    /* Unbekannte cmd-Werte: ignorieren (graceful degradation, §7.3) */
}

/* =========================================================================
 * Telemetrie-Stream
 * ========================================================================= */
static void send_telemetry(void)
{
    sns_data_t telem;
    sns_get_telemetry(&telem);

    mot_state_t mst;
    mot_get_state(&mst);

    uint32_t t_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);

    printf("{\"t\":%" PRIu32
           ",\"i\":%.3f,\"v\":%.1f,\"p\":%.1f"
           ",\"pwm\":%.1f,\"ss\":%d,\"ds\":%d}\n",
           t_ms,
           telem.current_a, telem.velocity_mms, telem.position_mm,
           mst.duty_pct,
           (int)sm_sys_get_state(), (int)sm_door_get_state());
}

/* =========================================================================
 * Init / Task
 * ========================================================================= */
esp_err_t comm_uart_init(void)
{
    ESP_LOGI(TAG, "UART-Telemetrie initialisiert (Baud: %u)", HODOR_UART_BAUD);
    return ESP_OK;
}

void comm_uart_task_func(void *arg)
{
    (void)arg;

    char rx_buf[UART_BUF_SIZE];
    int  rx_pos       = 0;
    int64_t last_send = 0;

    for (;;) {
        /* Stream-Periode aus Parameter */
        param_value_t v = {0};
        param_get(PARAM_UART_STREAM_MS, &v);
        int64_t period_us = (int64_t)v.u16 * 1000LL;

        /* Telemetrie senden wenn aktiv und Periode abgelaufen */
        if (s_stream_en) {
            int64_t now = esp_timer_get_time();
            if ((now - last_send) >= period_us) {
                send_telemetry();
                last_send = now;
            }
        }

        /* Empfangen – zeichenweise, non-blocking */
        int c = fgetc(stdin);
        if (c != EOF && c != 0xFF) {
            if (c == '\n' || c == '\r') {
                if (rx_pos > 0) {
                    rx_buf[rx_pos] = '\0';
                    process_json_cmd(rx_buf);
                    rx_pos = 0;
                }
            } else if (rx_pos < UART_BUF_SIZE - 1) {
                rx_buf[rx_pos++] = (char)c;
            }
        }

        /* Kurzes Delay – schnell genug für Zeichenempfang bei 115200 Baud */
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}
