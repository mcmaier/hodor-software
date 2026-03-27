/**
 * @file ctrl_pi.c
 * @brief Generischer PI-Regler mit Anti-Windup (Integrator-Clamp).
 */

#include "ctrl_pi.h"

void pi_init(pi_state_t *s, float kp, float ki,
             float out_min, float out_max, float dt)
{
    s->kp         = kp;
    s->ki         = ki;
    s->out_min    = out_min;
    s->out_max    = out_max;
    s->integrator = 0.0f;
    s->dt         = dt;
}

float pi_update(pi_state_t *s, float setpoint, float measured)
{
    float error = setpoint - measured;

    /* Proportional-Anteil */
    float out = s->kp * error;

    /* Integral-Anteil */
    s->integrator += s->ki * error * s->dt;

    /* Anti-Windup: Integrator auf Output-Grenzen begrenzen */
    if (s->integrator > s->out_max) s->integrator = s->out_max;
    if (s->integrator < s->out_min) s->integrator = s->out_min;

    out += s->integrator;

    /* Ausgangs-Begrenzung */
    if (out > s->out_max) out = s->out_max;
    if (out < s->out_min) out = s->out_min;

    return out;
}

void pi_reset(pi_state_t *s)
{
    s->integrator = 0.0f;
}

void pi_set_gains(pi_state_t *s, float kp, float ki)
{
    s->kp = kp;
    s->ki = ki;
}
