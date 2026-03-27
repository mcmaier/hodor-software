/**
 * @file mot_driver.c
 * @brief Motor-Treiber – MC33HB2002 H-Brücke über LEDC PWM + SPI-Konfiguration.
 *
 * PWM-Steuerung über Parallel-Mode (INPUT=0):
 *   IN1 = MOT_PWM_1, IN2 = MOT_PWM_2
 *   Vorwärts:  CH0 = duty, CH1 = 0   (IN1=H, IN2=H bei duty → Forward)
 *   Rückwärts: CH0 = 0,    CH1 = duty
 *   Bremse:    CH0 = max,  CH1 = max  (Freilauf High)
 *   Freilauf:  CH0 = 0,    CH1 = 0
 *
 * SPI-Konfiguration (MC33HB2002 Register):
 *   - H-Bridge-Modus, Parallel-Steuerung (INPUT=0)
 *   - Strombegrenzung: ILM=00 (5,4 A) als HW-Backstop
 *   - Slew Rate: SR=100 (2,0 V/µs, Default)
 *   - Thermal Management: TM=1, Active Current Limit: AL=1
 *
 * Startup nach Datenblatt: ENBL HIGH → 1 ms → Status lesen → clr_flt → Config
 */

#include "mot_driver.h"
#include "hal_ledc.h"
#include "hal_gpio.h"
#include "hal_spi.h"
#include "hodor_config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "mot_driver";

/* =========================================================================
 * MC33HB2002 SPI-Registerdefinitionen
 *
 * Frame-Format (16-bit):
 *   Bit 15     : R/W (1=Write, 0=Read)
 *   Bit 14:13  : Register-Adresse (A1:A0)
 *   Bit 12:0   : Daten
 * ========================================================================= */

/* Register-Adressen (Bit 14:13) */
#define DRV_REG_ID       0x0000u  /* 00: Device Identification (read-only) */
#define DRV_REG_STATUS   0x2000u  /* 01: Status (read/clear) */
#define DRV_REG_MASK     0x4000u  /* 10: Fault Status Mask */
#define DRV_REG_CONFIG   0x6000u  /* 11: Configuration and Control */
#define DRV_WRITE_BIT    0x8000u  /* Bit 15: Write */

/* Status-Register Bits (Register 0x01) */
#define DRV_ST_OT        (1u << 0)   /* Übertemperatur (latching) */
#define DRV_ST_TW        (1u << 1)   /* Temperaturwarnung */
#define DRV_ST_OC        (1u << 2)   /* Overcurrent (latching) */
#define DRV_ST_OL        (1u << 3)   /* Open Load */
#define DRV_ST_SCG1      (1u << 4)   /* Short-to-GND OUT1 (latching) */
#define DRV_ST_SCG2      (1u << 5)   /* Short-to-GND OUT2 (latching) */
#define DRV_ST_SCP1      (1u << 6)   /* Short-to-VPWR OUT1 (latching) */
#define DRV_ST_SCP2      (1u << 7)   /* Short-to-VPWR OUT2 (latching) */
#define DRV_ST_OV        (1u << 8)   /* Überspannung */
#define DRV_ST_UV        (1u << 9)   /* Unterspannung */
#define DRV_ST_CP_U      (1u << 10)  /* Ladepumpen-Unterspannung */
#define DRV_ST_FRM       (1u << 11)  /* SPI Framing Error */

/* Config-Register Bits (Register 0x03) */
#define DRV_CFG_VIN1     (1u << 0)   /* Virtueller Eingang 1 */
#define DRV_CFG_VIN2     (1u << 1)   /* Virtueller Eingang 2 */
#define DRV_CFG_INPUT    (1u << 2)   /* 1=SPI-Steuerung, 0=Parallel (IN1/IN2) */
#define DRV_CFG_MODE     (1u << 3)   /* 1=H-Bridge, 0=Half-Bridge */
#define DRV_CFG_EN       (1u << 4)   /* Output Enable */
#define DRV_CFG_SR0      (1u << 5)   /* Slew Rate Bit 0 */
#define DRV_CFG_SR1      (1u << 6)   /* Slew Rate Bit 1 */
#define DRV_CFG_SR2      (1u << 7)   /* Slew Rate Bit 2 */
#define DRV_CFG_ILM0     (1u << 8)   /* Strombegrenzung Bit 0 */
#define DRV_CFG_ILM1     (1u << 9)   /* Strombegrenzung Bit 1 */
#define DRV_CFG_AL       (1u << 10)  /* Active Current Limit */
#define DRV_CFG_TM       (1u << 11)  /* Thermal Management */
#define DRV_CFG_CL       (1u << 12)  /* Open Load Check */

/**
 * HODOR-Konfiguration:
 *   TM=1, AL=1, ILM=00 (5,4 A), SR=100 (2,0 V/µs),
 *   EN=1, MODE=1 (H-Bridge), INPUT=0 (Parallel), VIN2=0, VIN1=0
 */
#define DRV_CFG_HODOR    (DRV_CFG_TM | DRV_CFG_AL | DRV_CFG_SR2 | \
                          DRV_CFG_EN | DRV_CFG_MODE)

/* Frame für "alle Faults löschen" (Status-Register Write, alle Bits = 1) */
#define DRV_CLR_ALL_FAULTS  (DRV_WRITE_BIT | DRV_REG_STATUS | 0x0FFFu)

/* =========================================================================
 * Statischer Zustand
 * ========================================================================= */
static mot_state_t       s_state  = {0};
static SemaphoreHandle_t s_mutex  = NULL;

/* =========================================================================
 * SPI-Hilfsfunktionen
 * ========================================================================= */
static esp_err_t drv_spi_write(uint16_t reg_addr, uint16_t data)
{
    uint16_t frame = DRV_WRITE_BIT | reg_addr | (data & 0x1FFFu);
    return hal_spi_transfer16(frame, NULL);
}

static esp_err_t drv_spi_read(uint16_t reg_addr, uint16_t *data)
{
    uint16_t frame = reg_addr;  /* Bit 15 = 0 (Read), Datenbits ignoriert */
    uint16_t rx = 0;
    esp_err_t ret = hal_spi_transfer16(frame, &rx);
    if (ret == ESP_OK && data) {
        *data = rx & 0x1FFFu;  /* Nur Bit 12:0 sind Daten */
    }
    return ret;
}

static void drv_parse_status(uint16_t raw, mot_spi_status_t *st)
{
    st->ot   = (raw & DRV_ST_OT)   != 0;
    st->tw   = (raw & DRV_ST_TW)   != 0;
    st->oc   = (raw & DRV_ST_OC)   != 0;
    st->ol   = (raw & DRV_ST_OL)   != 0;
    st->scg1 = (raw & DRV_ST_SCG1) != 0;
    st->scg2 = (raw & DRV_ST_SCG2) != 0;
    st->scp1 = (raw & DRV_ST_SCP1) != 0;
    st->scp2 = (raw & DRV_ST_SCP2) != 0;
    st->ov   = (raw & DRV_ST_OV)   != 0;
    st->uv   = (raw & DRV_ST_UV)   != 0;
    st->cp_u = (raw & DRV_ST_CP_U) != 0;
    st->frm  = (raw & DRV_ST_FRM)  != 0;
}

/**
 * @brief MC33HB2002 Startup-Sequenz nach Datenblatt §12:
 *        1. ENBL HIGH
 *        2. 1 ms warten (t_TURN_ON)
 *        3. Status lesen
 *        4. clr_flt senden
 *        5. Config schreiben
 *        6. Config zurücklesen und verifizieren
 */
static esp_err_t drv_spi_init_sequence(void)
{
    esp_err_t ret;

    /* Schritt 1: ENBL HIGH (MOT_EN) – kurz für SPI-Init, wird danach wieder low */
    hal_gpio_set(HODOR_GPIO_MOT_EN, 1);

    /* Schritt 2: t_TURN_ON abwarten */
    vTaskDelay(pdMS_TO_TICKS(2));

    /* Schritt 3: Status lesen (erster SPI-Zugriff gibt Status zurück) */
    uint16_t status_raw = 0;
    ret = drv_spi_read(DRV_REG_STATUS, &status_raw);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI Status-Read: %s", esp_err_to_name(ret));
        hal_gpio_set(HODOR_GPIO_MOT_EN, 0);
        return ret;
    }
    ESP_LOGI(TAG, "MC33HB2002 Status nach Power-on: 0x%04x", status_raw);

    /* Schritt 4: Alle Faults löschen */
    ret = hal_spi_transfer16(DRV_CLR_ALL_FAULTS, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI clr_flt: %s", esp_err_to_name(ret));
        hal_gpio_set(HODOR_GPIO_MOT_EN, 0);
        return ret;
    }

    /* Schritt 5: Konfiguration schreiben */
    ret = drv_spi_write(DRV_REG_CONFIG, DRV_CFG_HODOR);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI Config-Write: %s", esp_err_to_name(ret));
        hal_gpio_set(HODOR_GPIO_MOT_EN, 0);
        return ret;
    }

    /* Schritt 6: Config zurücklesen und verifizieren */
    uint16_t cfg_readback = 0;
    ret = drv_spi_read(DRV_REG_CONFIG, &cfg_readback);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI Config-Read: %s", esp_err_to_name(ret));
        hal_gpio_set(HODOR_GPIO_MOT_EN, 0);
        return ret;
    }

    if (cfg_readback != DRV_CFG_HODOR) {
        ESP_LOGE(TAG, "SPI Config Verifikation: erwartet 0x%04x, gelesen 0x%04x",
                 DRV_CFG_HODOR, cfg_readback);
        hal_gpio_set(HODOR_GPIO_MOT_EN, 0);
        return ESP_ERR_INVALID_RESPONSE;
    }

    ESP_LOGI(TAG, "MC33HB2002 konfiguriert: ILM=00 (5,4A), SR=2,0V/µs, H-Bridge, Parallel");

    /* ENBL wieder LOW – wird erst bei mot_enable() aktiviert */
    hal_gpio_set(HODOR_GPIO_MOT_EN, 0);
    return ESP_OK;
}

/* =========================================================================
 * API
 * ========================================================================= */
esp_err_t mot_driver_init(void)
{
    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) return ESP_ERR_NO_MEM;

    s_state.duty_pct  = 0.0f;
    s_state.direction = MOT_DIR_COAST;
    s_state.enabled   = false;
    s_state.fault     = false;

    /* Sicherer Anfangszustand: Enable low, PWM = 0 */
    hal_gpio_set(HODOR_GPIO_MOT_EN, 0);
    hal_ledc_stop_all();

    /* MC33HB2002 SPI-Initialisierung */
    esp_err_t ret = drv_spi_init_sequence();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "MC33HB2002 SPI-Init fehlgeschlagen: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Motor-Treiber initialisiert");
    return ESP_OK;
}

esp_err_t mot_set_pwm(float duty_pct, mot_dir_t dir)
{
    if (!s_state.enabled) return ESP_OK;

    if (duty_pct < 0.0f) duty_pct = 0.0f;
    if (duty_pct > 100.0f) duty_pct = 100.0f;

    uint32_t duty_raw = (uint32_t)(duty_pct * HODOR_LEDC_DUTY_MAX / 100.0f);
    esp_err_t ret = ESP_OK;

    switch (dir) {
        case MOT_DIR_FORWARD:
            ret  = hal_ledc_set_duty(HODOR_LEDC_CH_PWM_1, duty_raw);
            ret |= hal_ledc_set_duty(HODOR_LEDC_CH_PWM_2, 0);
            break;
        case MOT_DIR_BACKWARD:
            ret  = hal_ledc_set_duty(HODOR_LEDC_CH_PWM_1, 0);
            ret |= hal_ledc_set_duty(HODOR_LEDC_CH_PWM_2, duty_raw);
            break;
        case MOT_DIR_BRAKE:
            ret  = hal_ledc_set_duty(HODOR_LEDC_CH_PWM_1, HODOR_LEDC_DUTY_MAX);
            ret |= hal_ledc_set_duty(HODOR_LEDC_CH_PWM_2, HODOR_LEDC_DUTY_MAX);
            break;
        case MOT_DIR_COAST:
            ret  = hal_ledc_stop_all();
            break;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_state.duty_pct  = (dir == MOT_DIR_BACKWARD) ? -duty_pct : duty_pct;
    s_state.direction = dir;
    xSemaphoreGive(s_mutex);

    return ret;
}

esp_err_t mot_enable(void)
{
    hal_gpio_set(HODOR_GPIO_MOT_EN, 1);
    /* t_TURN_ON des MC33HB2002: max 1 ms */
    vTaskDelay(pdMS_TO_TICKS(2));

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_state.enabled = true;
    xSemaphoreGive(s_mutex);
    ESP_LOGD(TAG, "Motor aktiviert");
    return ESP_OK;
}

esp_err_t mot_disable(void)
{
    hal_ledc_stop_all();
    hal_gpio_set(HODOR_GPIO_MOT_EN, 0);
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_state.enabled   = false;
    s_state.duty_pct  = 0.0f;
    s_state.direction = MOT_DIR_COAST;
    xSemaphoreGive(s_mutex);
    ESP_LOGD(TAG, "Motor deaktiviert");
    return ESP_OK;
}

esp_err_t mot_brake(void)
{
    return mot_set_pwm(100.0f, MOT_DIR_BRAKE);
}

esp_err_t mot_coast(void)
{
    return mot_set_pwm(0.0f, MOT_DIR_COAST);
}

void mot_get_state(mot_state_t *out)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    *out = s_state;
    xSemaphoreGive(s_mutex);
}

bool mot_check_fault(void)
{
    /* Schnellcheck: NFAULT-Pin (FS_B) */
    bool nfault_active = (hal_gpio_get(HODOR_GPIO_MOT_NFAULT) == 0);

    /* Detaillierter Check: SPI Status-Register */
    uint16_t status_raw = 0;
    mot_spi_status_t spi_st = {0};
    if (drv_spi_read(DRV_REG_STATUS, &status_raw) == ESP_OK) {
        drv_parse_status(status_raw, &spi_st);
    }

    bool any_fault = nfault_active ||
                     spi_st.ot || spi_st.oc ||
                     spi_st.scg1 || spi_st.scg2 ||
                     spi_st.scp1 || spi_st.scp2 ||
                     spi_st.uv;

    if (any_fault && !s_state.fault) {
        ESP_LOGW(TAG, "Fault erkannt: NFAULT=%d, Status=0x%04x "
                 "(OT=%d TW=%d OC=%d SCG=%d/%d SCP=%d/%d UV=%d OV=%d)",
                 nfault_active, status_raw,
                 spi_st.ot, spi_st.tw, spi_st.oc,
                 spi_st.scg1, spi_st.scg2,
                 spi_st.scp1, spi_st.scp2,
                 spi_st.uv, spi_st.ov);
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_state.fault      = any_fault;
    s_state.spi_status = spi_st;
    xSemaphoreGive(s_mutex);

    return any_fault;
}

esp_err_t mot_clear_faults(void)
{
    ESP_LOGI(TAG, "Lösche MC33HB2002 Faults");
    return hal_spi_transfer16(DRV_CLR_ALL_FAULTS, NULL);
}
