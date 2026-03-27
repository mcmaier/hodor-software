/**
 * @file cfg_nvs.h
 * @brief NVS-Wrapper und cfg_task für deferred NVS-Schreiboperationen.
 *
 * NVS-Commits können 10–100 ms dauern. HTTP/MQTT-Handler nutzen
 * cfg_write_queue → cfg_task_func, um nicht zu blockieren.
 */

#pragma once

#include "esp_err.h"
#include "hodor_param.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief NVS-Flash initialisieren und Namespace öffnen.
 *        Bei korruptem NVS: erase + reinit (Factory-Default-Verhalten).
 *        Muss als erstes aufgerufen werden (vor param_init).
 */
esp_err_t cfg_nvs_init(void);

/**
 * @brief Einen Parameterwert aus NVS laden (in p->val schreiben).
 * @return ESP_ERR_NVS_NOT_FOUND wenn Schlüssel nicht existiert (Erststart)
 */
esp_err_t cfg_nvs_load_param(param_desc_t *p);

/**
 * @brief Einen Parameterwert in NVS schreiben und committen.
 *        Blockiert bis nvs_commit() abgeschlossen.
 */
esp_err_t cfg_nvs_save_param(const param_desc_t *p);

/** @brief Alle NVS-Einträge im Namespace löschen. */
esp_err_t cfg_nvs_erase_all(void);

/**
 * @brief Anfrage in cfg_write_queue einstellen (non-blocking, von HTTP/MQTT nutzbar).
 *        cfg_task_func verarbeitet die Queue und ruft cfg_nvs_save_param() auf.
 */
esp_err_t cfg_nvs_queue_save(param_id_t id);

/**
 * @brief cfg_task – deferred NVS-Schreiboperationen verarbeiten.
 *        Wird von app_main auf Core 0 gestartet.
 */
void cfg_task_func(void *arg);

#ifdef __cplusplus
}
#endif
