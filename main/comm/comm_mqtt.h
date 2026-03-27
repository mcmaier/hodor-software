/**
 * @file comm_mqtt.h
 * @brief MQTT-Client – Befehle empfangen, Status publizieren.
 */

#pragma once

#include "esp_err.h"
#include "sm_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief MQTT-Task initialisieren. */
esp_err_t comm_mqtt_init(void);

/**
 * @brief comm_mqtt_task (Core 0, Prio 11).
 *        Wartet auf WiFi-Verbindung, verbindet mit Broker,
 *        empfängt Befehle → sm_event_queue, publiziert Status.
 */
void comm_mqtt_task_func(void *arg);

/** @brief Status-Nachricht von Core 1 an MQTT-Task senden. */
esp_err_t comm_mqtt_post_status(const comm_status_t *st);

/**
 * @brief MQTT-Broker-URI in NVS speichern.
 *        Wird vom HTTP-Konfigurationsportal aufgerufen.
 *        Wirksam nach Neustart.
 * @param uri  Broker-URI (z.B. "mqtt://192.168.1.100:1883"), NULL zum Löschen
 */
esp_err_t comm_mqtt_save_uri(const char *uri);

#ifdef __cplusplus
}
#endif
