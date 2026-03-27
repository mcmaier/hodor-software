/**
 * @file cfg_nvs.c
 * @brief NVS-Wrapper und cfg_task_func.
 */

#include "cfg_nvs.h"
#include "hodor_config.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "cfg_nvs";

static nvs_handle_t  s_nvs_handle = 0;
static QueueHandle_t s_write_queue = NULL;

/* =========================================================================
 * Init
 * ========================================================================= */
esp_err_t cfg_nvs_init(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS korrupt – wird gelöscht und neu initialisiert");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nvs_flash_init: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = nvs_open(HODOR_NVS_NAMESPACE, NVS_READWRITE, &s_nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open: %s", esp_err_to_name(ret));
        return ret;
    }

    s_write_queue = xQueueCreate(HODOR_QLEN_CFG_WRITE, sizeof(param_id_t));
    if (!s_write_queue) {
        ESP_LOGE(TAG, "cfg_write_queue Erstellung fehlgeschlagen");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "NVS bereit (Namespace: %s)", HODOR_NVS_NAMESPACE);
    return ESP_OK;
}

/* =========================================================================
 * Laden / Speichern
 * ========================================================================= */
esp_err_t cfg_nvs_load_param(param_desc_t *p)
{
    esp_err_t ret;
    switch (p->type) {
        case PARAM_TYPE_FLOAT: {
            /* NVS hat keinen float-Typ – als uint32_t (Bitcast) speichern */
            uint32_t raw = 0;
            ret = nvs_get_u32(s_nvs_handle, p->nvs_key, &raw);
            if (ret == ESP_OK) memcpy(&p->val.f, &raw, sizeof(float));
            break;
        }
        case PARAM_TYPE_UINT16:
            ret = nvs_get_u16(s_nvs_handle, p->nvs_key, &p->val.u16);
            break;
        case PARAM_TYPE_UINT8:
        case PARAM_TYPE_BOOL:
            ret = nvs_get_u8(s_nvs_handle, p->nvs_key, &p->val.u8);
            break;
        default:
            return ESP_ERR_INVALID_ARG;
    }
    return ret;
}

esp_err_t cfg_nvs_save_param(const param_desc_t *p)
{
    esp_err_t ret;
    switch (p->type) {
        case PARAM_TYPE_FLOAT: {
            uint32_t raw;
            memcpy(&raw, &p->val.f, sizeof(float));
            ret = nvs_set_u32(s_nvs_handle, p->nvs_key, raw);
            break;
        }
        case PARAM_TYPE_UINT16:
            ret = nvs_set_u16(s_nvs_handle, p->nvs_key, p->val.u16);
            break;
        case PARAM_TYPE_UINT8:
        case PARAM_TYPE_BOOL:
            ret = nvs_set_u8(s_nvs_handle, p->nvs_key, p->val.u8);
            break;
        default:
            return ESP_ERR_INVALID_ARG;
    }
    if (ret != ESP_OK) return ret;
    return nvs_commit(s_nvs_handle);
}

esp_err_t cfg_nvs_erase_all(void)
{
    esp_err_t ret = nvs_erase_all(s_nvs_handle);
    if (ret != ESP_OK) return ret;
    return nvs_commit(s_nvs_handle);
}

/* =========================================================================
 * Deferred-Write-Queue
 * ========================================================================= */
esp_err_t cfg_nvs_queue_save(param_id_t id)
{
    if (!s_write_queue) return ESP_ERR_INVALID_STATE;
    param_id_t item = id;
    if (xQueueSend(s_write_queue, &item, 0) != pdTRUE) {
        ESP_LOGW(TAG, "cfg_write_queue voll – Schreiben von 0x%04x verloren", id);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

/* =========================================================================
 * cfg_task – Core 0
 * ========================================================================= */
void cfg_task_func(void *arg)
{
    (void)arg;
    param_id_t id;

    for (;;) {
        if (xQueueReceive(s_write_queue, &id, portMAX_DELAY) == pdTRUE) {
            esp_err_t ret = param_save(id);
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "param_save(0x%04x): %s", id, esp_err_to_name(ret));
            }
        }
    }
}
