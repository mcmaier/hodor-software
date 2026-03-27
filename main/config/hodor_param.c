/**
 * @file hodor_param.c
 * @brief Parametertabelle und Zugriffs-API.
 *
 * param_table[] ist die einzige Quelle aller Laufzeitparameter.
 * Mutex-Schutz für Core-0/Core-1-Zugriff intern.
 */

#include "hodor_param.h"
#include "cfg_nvs.h"
#include "hodor_config.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "hodor_param";

/* =========================================================================
 * Parametertabelle (einzige Instanz)
 * Makro P_F / P_U16 / P_U8 / P_B für kompakte Tabelleneinträge.
 * ========================================================================= */
#define P_F(id_, name_, unit_, nvs_, def_, min_, max_, flags_) \
    { .id = (id_), .type = PARAM_TYPE_FLOAT,  .name = (name_), .unit = (unit_), \
      .nvs_key = (nvs_), .val = {.f = (def_)}, .def = {.f = (def_)},            \
      .min = {.f = (min_)}, .max = {.f = (max_)}, .flags = (flags_) }

#define P_U16(id_, name_, unit_, nvs_, def_, min_, max_, flags_) \
    { .id = (id_), .type = PARAM_TYPE_UINT16, .name = (name_), .unit = (unit_), \
      .nvs_key = (nvs_), .val = {.u16 = (def_)}, .def = {.u16 = (def_)},        \
      .min = {.u16 = (min_)}, .max = {.u16 = (max_)}, .flags = (flags_) }

#define P_U8(id_, name_, unit_, nvs_, def_, min_, max_, flags_) \
    { .id = (id_), .type = PARAM_TYPE_UINT8,  .name = (name_), .unit = (unit_), \
      .nvs_key = (nvs_), .val = {.u8 = (def_)}, .def = {.u8 = (def_)},          \
      .min = {.u8 = (min_)}, .max = {.u8 = (max_)}, .flags = (flags_) }

#define P_B(id_, name_, nvs_, def_, flags_) \
    { .id = (id_), .type = PARAM_TYPE_BOOL,   .name = (name_), .unit = "-",     \
      .nvs_key = (nvs_), .val = {.b = (def_)}, .def = {.b = (def_)},            \
      .min = {.b = 0}, .max = {.b = 1}, .flags = (flags_) }

#define PERSIST  PARAM_FLAG_PERSIST
#define STREAM   PARAM_FLAG_STREAM
#define RO       PARAM_FLAG_READONLY
#define REBOOT   PARAM_FLAG_REBOOT

static param_desc_t param_table[] = {
    /* Block 0x0000 – Motor */
    P_F  (PARAM_MOTOR_MAX_A,       "motor_max_a",     "A",    "mot_max_a",   3.0f,  0.1f,    5.0f,    PERSIST),
    P_U16(PARAM_MOTOR_PWM_FREQ_HZ, "motor_pwm_freq",  "Hz",   "mot_pwm_hz",  20000, 10000,   40000,   PERSIST|REBOOT),
    P_U16(PARAM_OVERCURRENT_MS,    "overcurrent_ms",  "ms",   "oc_ms",       200,   10,      2000,    PERSIST),

    /* Block 0x0100 – Tür */
    P_U16(PARAM_DOOR_OPEN_MM,      "door_open_mm",    "mm",   "door_opn_mm", 800,   50,      2000,    PERSIST),
    P_U16(PARAM_POS_TOLERANCE_MM,  "pos_tol_mm",      "mm",   "pos_tol_mm",  5,     1,       50,      PERSIST),
    P_U16(PARAM_AUTOCLOSE_S,       "autoclose_s",     "s",    "autocls_s",   0,     0,       300,     PERSIST),

    /* Block 0x0200 – Regler */
    P_F  (PARAM_CTRL_I_KP,         "ctrl_i_kp",       "-",    "i_kp",        1.0f,  0.0f,    100.0f,  PERSIST),
    P_F  (PARAM_CTRL_I_KI,         "ctrl_i_ki",       "-",    "i_ki",        10.0f, 0.0f,    1000.0f, PERSIST),
    P_F  (PARAM_CTRL_V_KP,         "ctrl_v_kp",       "-",    "v_kp",        0.5f,  0.0f,    100.0f,  PERSIST),
    P_F  (PARAM_CTRL_V_KI,         "ctrl_v_ki",       "-",    "v_ki",        2.0f,  0.0f,    1000.0f, PERSIST),
    P_F  (PARAM_CTRL_P_KP,         "ctrl_p_kp",       "-",    "p_kp",        0.1f,  0.0f,    10.0f,   PERSIST),
    P_F  (PARAM_V_MAX_MMS,         "v_max_mms",       "mm/s", "v_max",       150.0f,10.0f,   500.0f,  PERSIST),
    P_F  (PARAM_V_MIN_THRESH_MMS,  "v_min_thresh",    "mm/s", "v_min_thr",   2.0f,  0.5f,    20.0f,   PERSIST),

    /* Block 0x0300 – Ruckbegrenzer */
    P_B  (PARAM_JERK_LIMIT_EN,     "jerk_limit_en",   "jrk_en",  0,  PERSIST),
    P_F  (PARAM_RAMP_JERK,         "ramp_jerk",       "mm/s3","ramp_jrk", 500.0f,  1.0f, 10000.0f, PERSIST),
    P_F  (PARAM_RAMP_ACCEL_MAX,    "ramp_accel_max",  "mm/s2","ramp_acc", 200.0f,  1.0f,  1000.0f, PERSIST),

    /* Block 0x0400 – I/O */
    P_U8 (PARAM_INPUT_MODE_1,      "input_mode_1",    "-",    "inp_mode1",   0,     0,       3,       PERSIST),
    P_U8 (PARAM_INPUT_MODE_2,      "input_mode_2",    "-",    "inp_mode2",   0,     0,       3,       PERSIST),
    P_U8 (PARAM_INPUT_MODE_3,      "input_mode_3",    "-",    "inp_mode3",   0,     0,       3,       PERSIST),
    P_U8 (PARAM_INPUT_MODE_4,      "input_mode_4",    "-",    "inp_mode4",   0,     0,       3,       PERSIST),

    /* Block 0x0500 – Kommunikation */
    P_B  (PARAM_MQTT_EN,           "mqtt_en",         "mqtt_en",  0,  PERSIST),
    P_U16(PARAM_UART_STREAM_MS,    "uart_stream_ms",  "ms",   "uart_str_ms", 100,   10,      5000,    PERSIST),

    /* Block 0x0F00 – Telemetrie (Read-only) */
    P_F  (PARAM_MEAS_CURRENT_A,    "meas_current_a",  "A",    "",            0.0f,  -5.0f,   5.0f,    RO|STREAM),
    P_F  (PARAM_MEAS_VELOCITY_MMS, "meas_vel_mms",    "mm/s", "",            0.0f,  -500.0f, 500.0f,  RO|STREAM),
    P_F  (PARAM_MEAS_POSITION_MM,  "meas_pos_mm",     "mm",   "",            0.0f,  0.0f,    2000.0f, RO|STREAM),
    P_F  (PARAM_MEAS_PWM_DUTY_PCT, "meas_pwm_pct",    "%",    "",            0.0f,  -100.0f, 100.0f,  RO|STREAM),
    P_U8 (PARAM_MEAS_SYS_STATE,    "meas_sys_state",  "-",    "",            0,     0,       4,       RO|STREAM),
    P_U8 (PARAM_MEAS_DOOR_STATE,   "meas_door_state", "-",    "",            0,     0,       6,       RO|STREAM),
};

#define PARAM_COUNT (sizeof(param_table) / sizeof(param_table[0]))

static SemaphoreHandle_t s_mutex = NULL;

/* =========================================================================
 * Hilfsfunktion – Eintrag nach ID suchen
 * ========================================================================= */
static param_desc_t *find_param(param_id_t id)
{
    for (size_t i = 0; i < PARAM_COUNT; i++) {
        if (param_table[i].id == (uint16_t)id) {
            return &param_table[i];
        }
    }
    return NULL;
}

/* =========================================================================
 * API-Implementierung
 * ========================================================================= */
esp_err_t param_init(void)
{
    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) {
        ESP_LOGE(TAG, "Mutex-Erstellung fehlgeschlagen");
        return ESP_ERR_NO_MEM;
    }

    /* NVS-Werte laden */
    for (size_t i = 0; i < PARAM_COUNT; i++) {
        param_desc_t *p = &param_table[i];
        if (!(p->flags & PARAM_FLAG_PERSIST) || p->nvs_key[0] == '\0') continue;
        /* Fehler beim Laden (z.B. erster Start) → Default bleibt */
        esp_err_t ret = cfg_nvs_load_param(p);
        if (ret != ESP_OK && ret != ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGW(TAG, "NVS-Laden '%s': %s – Default wird verwendet",
                     p->name, esp_err_to_name(ret));
        }
    }

    ESP_LOGI(TAG, "%u Parameter initialisiert", (unsigned)PARAM_COUNT);
    return ESP_OK;
}

esp_err_t param_get(param_id_t id, param_value_t *val)
{
    param_desc_t *p = find_param(id);
    if (!p) return ESP_ERR_NOT_FOUND;

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    *val = p->val;
    xSemaphoreGive(s_mutex);
    return ESP_OK;
}

esp_err_t param_set(param_id_t id, param_value_t val)
{
    param_desc_t *p = find_param(id);
    if (!p)                          return ESP_ERR_NOT_FOUND;
    if (p->flags & PARAM_FLAG_READONLY) return ESP_ERR_NOT_SUPPORTED;

    /* Limit-Prüfung */
    bool in_range = true;
    switch (p->type) {
        case PARAM_TYPE_FLOAT:  in_range = (val.f   >= p->min.f   && val.f   <= p->max.f);   break;
        case PARAM_TYPE_UINT16: in_range = (val.u16 >= p->min.u16 && val.u16 <= p->max.u16); break;
        case PARAM_TYPE_UINT8:  in_range = (val.u8  >= p->min.u8  && val.u8  <= p->max.u8);  break;
        case PARAM_TYPE_BOOL:   in_range = (val.b == 0 || val.b == 1);                        break;
    }
    if (!in_range) return ESP_ERR_INVALID_ARG;

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    p->val = val;
    xSemaphoreGive(s_mutex);
    return ESP_OK;
}

esp_err_t param_set_default(param_id_t id)
{
    param_desc_t *p = find_param(id);
    if (!p) return ESP_ERR_NOT_FOUND;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    p->val = p->def;
    xSemaphoreGive(s_mutex);
    return ESP_OK;
}

esp_err_t param_reset_all(void)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    for (size_t i = 0; i < PARAM_COUNT; i++) {
        param_table[i].val = param_table[i].def;
    }
    xSemaphoreGive(s_mutex);
    return ESP_OK;
}

esp_err_t param_save(param_id_t id)
{
    param_desc_t *p = find_param(id);
    if (!p)                              return ESP_ERR_NOT_FOUND;
    if (!(p->flags & PARAM_FLAG_PERSIST)) return ESP_ERR_NOT_SUPPORTED;
    if (p->nvs_key[0] == '\0')           return ESP_ERR_NOT_SUPPORTED;
    return cfg_nvs_save_param(p);
}

esp_err_t param_save_all(void)
{
    esp_err_t last_err = ESP_OK;
    for (size_t i = 0; i < PARAM_COUNT; i++) {
        if (!(param_table[i].flags & PARAM_FLAG_PERSIST)) continue;
        if (param_table[i].nvs_key[0] == '\0')            continue;
        esp_err_t ret = cfg_nvs_save_param(&param_table[i]);
        if (ret != ESP_OK) last_err = ret;
    }
    return last_err;
}

const param_desc_t *param_get_desc(param_id_t id)
{
    return find_param(id);
}

size_t param_count(void)
{
    return PARAM_COUNT;
}

const param_desc_t *param_get_by_index(size_t index)
{
    if (index >= PARAM_COUNT) return NULL;
    return &param_table[index];
}
