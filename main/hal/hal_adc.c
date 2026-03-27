/**
 * @file hal_adc.c
 * @brief HAL – ADC Oneshot + Kalibrierung für ACS725-Strommessung.
 *
 * ACS725 ist an einem ADC1-Kanal angeschlossen. Der Kanal-Pin wird über
 * hodor_config.h bestimmt – hier noch als Platzhalter ADC1_CHANNEL_0.
 * Nach PCB-Layout durch den korrekten Kanal ersetzen.
 */

#include "hal_adc.h"
#include "hodor_config.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_log.h"

/* TODO: Kanal nach PCB-Layout in hodor_config.h definieren */
#define HODOR_ADC_UNIT    ADC_UNIT_1
#define HODOR_ADC_CHANNEL ADC_CHANNEL_0   /* Platzhalter */
#define HODOR_ADC_ATTEN   ADC_ATTEN_DB_12 /* 0–3,1 V Bereich bei 3,3 V */

static const char *TAG = "hal_adc";

static adc_oneshot_unit_handle_t s_adc_handle  = NULL;
static adc_cali_handle_t         s_cali_handle = NULL;
static bool                      s_cali_ok     = false;

esp_err_t hal_adc_init(void)
{
    esp_err_t ret;

    /* ADC-Einheit öffnen */
    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id  = HODOR_ADC_UNIT,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    ret = adc_oneshot_new_unit(&unit_cfg, &s_adc_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "adc_oneshot_new_unit: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Kanal konfigurieren */
    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten    = HODOR_ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ret = adc_oneshot_config_channel(s_adc_handle, HODOR_ADC_CHANNEL, &chan_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "adc_oneshot_config_channel: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Kalibrierung – bevorzugt Curve-Fitting (ESP32-S3 eFuse) */
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id  = HODOR_ADC_UNIT,
        .chan     = HODOR_ADC_CHANNEL,
        .atten    = HODOR_ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ret = adc_cali_create_scheme_curve_fitting(&cali_cfg, &s_cali_handle);
    if (ret == ESP_OK) {
        s_cali_ok = true;
        ESP_LOGI(TAG, "Kalibrierung: Curve Fitting");
    }
#endif
    if (!s_cali_ok) {
        ESP_LOGW(TAG, "Keine eFuse-Kalibrierung – Rohwerte werden skaliert");
    }

    ESP_LOGI(TAG, "ADC initialisiert (Kanal %d, Dämpfung %d)", HODOR_ADC_CHANNEL, HODOR_ADC_ATTEN);
    return ESP_OK;
}

esp_err_t hal_adc_read_mv(int *out_mv)
{
    int raw = 0;
    esp_err_t ret = adc_oneshot_read(s_adc_handle, HODOR_ADC_CHANNEL, &raw);
    if (ret != ESP_OK) return ret;

    if (s_cali_ok) {
        return adc_cali_raw_to_voltage(s_cali_handle, raw, out_mv);
    } else {
        /* Lineare Näherung ohne Kalibrierung */
        *out_mv = (int)((raw * (int)HODOR_ADC_VREF_MV) / 4095);
        return ESP_OK;
    }
}

void hal_adc_deinit(void)
{
    if (s_cali_ok && s_cali_handle) {
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
        adc_cali_delete_scheme_curve_fitting(s_cali_handle);
#endif
        s_cali_handle = NULL;
        s_cali_ok     = false;
    }
    if (s_adc_handle) {
        adc_oneshot_del_unit(s_adc_handle);
        s_adc_handle = NULL;
    }
}
