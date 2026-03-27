/**
 * @file comm_wifi.h
 * @brief WiFi-Task – STA/AP-Modus, Event-Group-Pflege.
 */

#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief WiFi initialisieren (NVS muss bereit sein).
 *        Erstellt comm_event_group.
 */
esp_err_t comm_wifi_init(void);

/**
 * @brief comm_wifi_task (Core 0, Prio 12).
 *        Verbindet mit gespeichertem AP oder öffnet eigenen AP (Fallback).
 */
void comm_wifi_task_func(void *arg);

/**
 * @brief Event-Group-Handle für comm-Tasks (WiFi/MQTT/HTTP-Synchronisierung).
 *        Wird in comm_wifi_init() erstellt.
 */
EventGroupHandle_t comm_get_event_group(void);

#ifdef __cplusplus
}
#endif
