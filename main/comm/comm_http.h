/**
 * @file comm_http.h
 * @brief HTTP-Webserver – Captive Portal und REST-API.
 */

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief HTTP-Server initialisieren. */
esp_err_t comm_http_init(void);

/**
 * @brief comm_http_task (Core 0, Prio 10).
 *        Startet esp_http_server nach WiFi-Verbindung.
 */
void comm_http_task_func(void *arg);

#ifdef __cplusplus
}
#endif
