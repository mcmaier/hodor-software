/**
 * @file ctrl_loop.c
 * @brief Kaskadenregler – Positions-, Geschwindigkeits- und Stromregler.
 *
 * Kein dynamischer Speicher in der Regler-Loop.
 * Parameter werden beim Aktivieren und periodisch alle 100 ms aus param_table gelesen.
 */

#include "ctrl_loop.h"
#include "ctrl_pi.h"
#include "sns_task.h"
#include "mot_driver.h"
#include "sm_system.h"
#include "sm_door.h"
#include "hodor_config.h"
#include "hodor_param.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "esp_timer.h"
#include "esp_log.h"

static const char *TAG = "ctrl_loop";

/* =========================================================================
 * Statischer Zustand (keine Allokation zur Laufzeit)
 * ========================================================================= */
static SemaphoreHandle_t  s_timer_sem    = NULL;
static esp_timer_handle_t s_ctrl_timer   = NULL;
static volatile uint32_t  s_enabled      = 0;
static volatile float     s_target_mm    = 0.0f;

static pi_state_t s_pi_current;
static pi_state_t s_pi_velocity;
static pi_state_t s_pi_position;

/* Divider-Zähler */
static uint32_t s_vel_tick = 0;
static uint32_t s_pos_tick = 0;

/* Blockier-Erkennung */
static uint32_t s_block_ticks = 0;

/* Periodisches Parameter-Reload (alle 100 ms = HODOR_CTRL_POS_DIVIDER Ticks) */
static uint32_t s_param_reload_counter = 0;
#define PARAM_RELOAD_TICKS  (100u * HODOR_CTRL_VEL_DIVIDER)  /* 100 ms */

static void IRAM_ATTR ctrl_timer_cb(void *arg)
{
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(s_timer_sem, &woken);
    if (woken) portYIELD_FROM_ISR();
}

/* =========================================================================
 * Hilfsfunktion: Regler-Parameter aus Tabelle laden
 * ========================================================================= */
static void load_ctrl_params(void)
{
    param_value_t v = {0};
    float kp, ki;

    param_get(PARAM_CTRL_I_KP, &v); kp = v.f;
    param_get(PARAM_CTRL_I_KI, &v); ki = v.f;
    pi_set_gains(&s_pi_current, kp, ki);

    param_get(PARAM_CTRL_V_KP, &v); kp = v.f;
    param_get(PARAM_CTRL_V_KI, &v); ki = v.f;
    pi_set_gains(&s_pi_velocity, kp, ki);

    param_get(PARAM_CTRL_P_KP, &v); kp = v.f;
    pi_set_gains(&s_pi_position, kp, 0.0f);
}

/* =========================================================================
 * API
 * ========================================================================= */
esp_err_t ctrl_loop_init(void)
{
    s_timer_sem = xSemaphoreCreateBinary();
    if (!s_timer_sem) return ESP_ERR_NO_MEM;

    /* PI-Regler initialisieren mit Platzhalter-Defaults */
    pi_init(&s_pi_current,  1.0f,  10.0f, -100.0f, 100.0f,
            HODOR_CTRL_PERIOD_US * 1e-6f);
    pi_init(&s_pi_velocity, 0.5f,   2.0f, -100.0f, 100.0f,
            HODOR_CTRL_PERIOD_US * HODOR_CTRL_VEL_DIVIDER * 1e-6f);
    pi_init(&s_pi_position, 0.1f,   0.0f, -150.0f, 150.0f,
            HODOR_CTRL_PERIOD_US * HODOR_CTRL_POS_DIVIDER * 1e-6f);

    load_ctrl_params();

    s_enabled   = 0;
    s_target_mm = 0.0f;

    /* Timer erstellen, aber NICHT starten – Start via ctrl_loop_start_timer()
     * in app_main Phase 6 nach erfolgreicher Hardware-Verifikation. */
    esp_timer_create_args_t args = {
        .callback        = ctrl_timer_cb,
        .arg             = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name            = "ctrl_100us",
    };
    esp_err_t ret = esp_timer_create(&args, &s_ctrl_timer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_timer_create: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Regelkreis initialisiert (Timer bereit, nicht gestartet)");
    return ESP_OK;
}

esp_err_t ctrl_loop_start_timer(void)
{
    if (!s_ctrl_timer) return ESP_ERR_INVALID_STATE;

    esp_err_t ret = esp_timer_start_periodic(s_ctrl_timer, HODOR_CTRL_PERIOD_US);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_timer_start_periodic: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "Regler-Timer gestartet (%u µs)", HODOR_CTRL_PERIOD_US);
    return ESP_OK;
}

void ctrl_enable(void)
{
    load_ctrl_params();
    pi_reset(&s_pi_current);
    pi_reset(&s_pi_velocity);
    pi_reset(&s_pi_position);
    s_vel_tick   = 0;
    s_pos_tick   = 0;
    s_block_ticks = 0;
    s_enabled    = 1;
    ESP_LOGD(TAG, "Regelkreis aktiv, Ziel: %.1f mm", s_target_mm);
}

void ctrl_disable(void)
{
    s_enabled = 0;
    mot_disable();
    ESP_LOGD(TAG, "Regelkreis deaktiviert");
}

void ctrl_set_target(float target_mm)
{
    s_target_mm = target_mm;
}

/* =========================================================================
 * ctrl_task – Haupt-Loop
 * ========================================================================= */
void ctrl_task_func(void *arg)
{
    (void)arg;

    QueueHandle_t sns_q    = sns_get_data_queue();
    sns_data_t    sns_data = {0};
    float         vel_setpoint = 0.0f;
    float         cur_setpoint = 0.0f;

    for (;;) {
        /* Warte auf 100-µs-Timer-Semaphor */
        xSemaphoreTake(s_timer_sem, portMAX_DELAY);

        if (!s_enabled) continue;

        /* Neueste Sensordaten holen (neueste verfügbare, ohne Blockieren) */
        while (xQueueReceive(sns_q, &sns_data, 0) == pdTRUE) { /* leert Queue */ }

        /* ── Stromregler (100 µs) ───────────────────────────────────────── */
        float pwm_out = pi_update(&s_pi_current, cur_setpoint, sns_data.current_a);

        /* PWM-Ausgabe */
        mot_dir_t dir = (pwm_out >= 0.0f) ? MOT_DIR_FORWARD : MOT_DIR_BACKWARD;
        mot_set_pwm((pwm_out >= 0.0f) ? pwm_out : -pwm_out, dir);

        /* ── Geschwindigkeitsregler (1 ms) ─────────────────────────────── */
        s_vel_tick++;
        if (s_vel_tick >= HODOR_CTRL_VEL_DIVIDER) {
            s_vel_tick = 0;
            cur_setpoint = pi_update(&s_pi_velocity, vel_setpoint,
                                     sns_data.velocity_mms);
        }

        /* ── Positionsregler (10 ms) ────────────────────────────────────── */
        s_pos_tick++;
        if (s_pos_tick >= HODOR_CTRL_POS_DIVIDER) {
            s_pos_tick = 0;

            param_value_t v = {0};
            param_get(PARAM_V_MAX_MMS, &v);
            pi_init(&s_pi_position, s_pi_position.kp, 0.0f,
                    -v.f, v.f,
                    HODOR_CTRL_PERIOD_US * HODOR_CTRL_POS_DIVIDER * 1e-6f);

            vel_setpoint = pi_update(&s_pi_position, s_target_mm,
                                     sns_data.position_mm);

            /* Zielposition erreicht? */
            param_get(PARAM_POS_TOLERANCE_MM, &v);
            float err = s_target_mm - sns_data.position_mm;
            if (err < 0.0f) err = -err;
            if (err <= (float)v.u16) {
                sm_event_t evt = { .id = EVT_POS_REACHED, .data = 0 };
                sm_door_post_event(&evt);
            }
        }

        /* ── Blockier-Erkennung ─────────────────────────────────────────── */
        {
            param_value_t v = {0};
            param_get(PARAM_MOTOR_MAX_A, &v);
            float i_abs = sns_data.current_a;
            if (i_abs < 0.0f) i_abs = -i_abs;
            if (i_abs >= v.f) {
                s_block_ticks++;
                param_get(PARAM_OVERCURRENT_MS, &v);
                uint32_t block_threshold = v.u16 * HODOR_CTRL_VEL_DIVIDER;
                if (s_block_ticks >= block_threshold) {
                    s_block_ticks = 0;
                    sm_event_t evt = { .id = EVT_BLOCKED, .data = 0 };
                    sm_door_post_event(&evt);
                }
            } else {
                s_block_ticks = 0;
            }
        }

        /* ── Periodischer Parameter-Reload ─────────────────────────────── */
        s_param_reload_counter++;
        if (s_param_reload_counter >= PARAM_RELOAD_TICKS) {
            s_param_reload_counter = 0;
            load_ctrl_params();
        }
    }
}
