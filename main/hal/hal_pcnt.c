/**
 * @file hal_pcnt.c
 * @brief HAL – PCNT Quadraturdecoder für Inkrementalencoder.
 *
 * Verwendet ESP-IDF PCNT-Treiber (driver/pulse_cnt.h).
 * Überlauf-Callback akkumuliert Zähler auf int32_t.
 */

#include "hal_pcnt.h"
#include "hodor_config.h"
#include "driver/pulse_cnt.h"
#include "esp_log.h"

static const char *TAG = "hal_pcnt";

#define PCNT_HIGH_LIMIT   ( 32767)
#define PCNT_LOW_LIMIT    (-32768)

static pcnt_unit_handle_t s_pcnt_unit    = NULL;
static pcnt_channel_handle_t s_chan_a    = NULL;
static pcnt_channel_handle_t s_chan_b    = NULL;
static volatile int32_t   s_accum        = 0;

static bool pcnt_overflow_cb(pcnt_unit_handle_t unit,
                              const pcnt_watch_event_data_t *edata,
                              void *user_ctx)
{
    /* Überlauf akkumulieren – wird aus ISR-Kontext aufgerufen */
    if (edata->watch_point_value == PCNT_HIGH_LIMIT) {
        s_accum += PCNT_HIGH_LIMIT;
    } else {
        s_accum += PCNT_LOW_LIMIT;
    }
    return false; /* kein Context-Switch nötig */
}

esp_err_t hal_pcnt_init(void)
{
    esp_err_t ret;

    pcnt_unit_config_t unit_cfg = {
        .high_limit = PCNT_HIGH_LIMIT,
        .low_limit  = PCNT_LOW_LIMIT,
    };
    ret = pcnt_new_unit(&unit_cfg, &s_pcnt_unit);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "pcnt_new_unit: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Glitch-Filter: 1000 ns – unterdrückt Kontaktprellen */
    pcnt_glitch_filter_config_t filter_cfg = { .max_glitch_ns = 1000 };
    ret = pcnt_unit_set_glitch_filter(s_pcnt_unit, &filter_cfg);
    if (ret != ESP_OK) return ret;

    /* Kanal A: Flanken auf ENC_A, Level ENC_B bestimmt Richtung */
    pcnt_chan_config_t ch_a_cfg = {
        .edge_gpio_num  = HODOR_GPIO_ENC_A,
        .level_gpio_num = HODOR_GPIO_ENC_B,
    };
    ret = pcnt_new_channel(s_pcnt_unit, &ch_a_cfg, &s_chan_a);
    if (ret != ESP_OK) return ret;
    pcnt_channel_set_edge_action(s_chan_a,
        PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_DECREASE);
    pcnt_channel_set_level_action(s_chan_a,
        PCNT_CHANNEL_LEVEL_ACTION_KEEP,   PCNT_CHANNEL_LEVEL_ACTION_INVERSE);

    /* Kanal B: Flanken auf ENC_B, Level ENC_A bestimmt Richtung */
    pcnt_chan_config_t ch_b_cfg = {
        .edge_gpio_num  = HODOR_GPIO_ENC_B,
        .level_gpio_num = HODOR_GPIO_ENC_A,
    };
    ret = pcnt_new_channel(s_pcnt_unit, &ch_b_cfg, &s_chan_b);
    if (ret != ESP_OK) return ret;
    pcnt_channel_set_edge_action(s_chan_b,
        PCNT_CHANNEL_EDGE_ACTION_DECREASE, PCNT_CHANNEL_EDGE_ACTION_INCREASE);
    pcnt_channel_set_level_action(s_chan_b,
        PCNT_CHANNEL_LEVEL_ACTION_KEEP,   PCNT_CHANNEL_LEVEL_ACTION_INVERSE);

    /* Überlauf-Watchpoints */
    ret = pcnt_unit_add_watch_point(s_pcnt_unit, PCNT_HIGH_LIMIT);
    if (ret != ESP_OK) return ret;
    ret = pcnt_unit_add_watch_point(s_pcnt_unit, PCNT_LOW_LIMIT);
    if (ret != ESP_OK) return ret;

    /* Callback für Überlauf-Tracking */
    pcnt_event_callbacks_t cbs = { .on_reach = pcnt_overflow_cb };
    ret = pcnt_unit_register_event_callbacks(s_pcnt_unit, &cbs, NULL);
    if (ret != ESP_OK) return ret;

    ret = pcnt_unit_enable(s_pcnt_unit);
    if (ret != ESP_OK) return ret;
    ret = pcnt_unit_clear_count(s_pcnt_unit);
    if (ret != ESP_OK) return ret;
    ret = pcnt_unit_start(s_pcnt_unit);
    if (ret != ESP_OK) return ret;

    ESP_LOGI(TAG, "PCNT Quadratur-Decoder initialisiert");
    return ESP_OK;
}

esp_err_t hal_pcnt_get_count(int32_t *count)
{
    int hw_count = 0;
    esp_err_t ret = pcnt_unit_get_count(s_pcnt_unit, &hw_count);
    if (ret != ESP_OK) return ret;
    *count = s_accum + (int32_t)hw_count;
    return ESP_OK;
}

esp_err_t hal_pcnt_clear(void)
{
    s_accum = 0;
    return pcnt_unit_clear_count(s_pcnt_unit);
}

void hal_pcnt_deinit(void)
{
    if (s_pcnt_unit) {
        pcnt_unit_stop(s_pcnt_unit);
        pcnt_unit_disable(s_pcnt_unit);
        if (s_chan_a) { pcnt_del_channel(s_chan_a); s_chan_a = NULL; }
        if (s_chan_b) { pcnt_del_channel(s_chan_b); s_chan_b = NULL; }
        pcnt_del_unit(s_pcnt_unit);
        s_pcnt_unit = NULL;
    }
}
