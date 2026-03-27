/**
 * @file comm_uart.c
 * @brief UART-Telemetrie-Stream und JSON-Parameter-Protokoll.
 *
 * Protokoll gemäß docs/parameters.md §7.
 * Stream-Format (newline-terminiert):
 *   {"t":1234,"i":1.23,"v":150.5,"p":412.0,"pwm":45.2,"ss":3,"ds":1}
 */

#include "comm_uart.h"
#include "sns_task.h"
#include "sm_system.h"
#include "hodor_param.h"
#include "cfg_nvs.h"
#include "hodor_config.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <stdio.h>
#include <string.h>
#include <inttypes.h>

static const char *TAG = "comm_uart";

#define UART_PORT      UART_NUM_0
#define UART_BUF_SIZE  512

static bool s_stream_en = false;

esp_err_t comm_uart_init(void)
{
    /* UART0 wird von ESP-IDF Logging bereits verwendet.
     * Für Produktionssoftware eigenen UART-Port konfigurieren.
     * Hier: Logging-UART mitnutzen via printf/scanf. */
    ESP_LOGI(TAG, "UART-Telemetrie initialisiert (Baud: %u)", HODOR_UART_BAUD);
    return ESP_OK;
}

static void process_json_cmd(const char *buf)
{
    if (strstr(buf, "\"stream\"")) {
        s_stream_en = (strstr(buf, "true") != NULL);
        return;
    }

    if (strstr(buf, "\"list\"")) {
        /* Alle Parameter ausgeben */
        for (size_t i = 0; i < param_count(); i++) {
            const param_desc_t *p = param_get_by_index(i);
            if (!p) continue;
            char line[128];
            if (p->type == PARAM_TYPE_FLOAT) {
                snprintf(line, sizeof(line),
                         "{\"id\":\"0x%04x\",\"name\":\"%s\",\"val\":%.4f,\"unit\":\"%s\"}\n",
                         p->id, p->name, p->val.f, p->unit);
            } else {
                snprintf(line, sizeof(line),
                         "{\"id\":\"0x%04x\",\"name\":\"%s\",\"val\":%u,\"unit\":\"%s\"}\n",
                         p->id, p->name, (unsigned)p->val.u16, p->unit);
            }
            printf("%s", line);
        }
        return;
    }

    /* get / set – einfache Implementierung, vollständig in docs/parameters.md §7 */
    if (strstr(buf, "\"get\"")) {
        /* TODO: ID parsen und Wert ausgeben */
        return;
    }

    if (strstr(buf, "\"set\"")) {
        /* TODO: ID und Wert parsen, param_set() aufrufen, cfg_nvs_queue_save() */
        return;
    }
}

void comm_uart_task_func(void *arg)
{
    (void)arg;

    char rx_buf[UART_BUF_SIZE];
    int  rx_pos = 0;

    for (;;) {
        /* Stream senden */
        if (s_stream_en) {
            sns_data_t telem;
            sns_get_telemetry(&telem);
            uint32_t t_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
            printf("{\"t\":%" PRIu32 ",\"i\":%.3f,\"v\":%.1f,\"p\":%.1f,\"ss\":%d}\n",
                   t_ms, telem.current_a, telem.velocity_mms,
                   telem.position_mm, (int)sm_sys_get_state());
        }

        /* Empfangen – zeichenweise via UART0 */
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

        /* Periode aus Parameter */
        param_value_t v = {0};
        param_get(PARAM_UART_STREAM_MS, &v);
        vTaskDelay(pdMS_TO_TICKS(v.u16));
    }
}
