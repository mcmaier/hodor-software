/**
 * @file ctrl_pi.h
 * @brief Generischer PI-Regler mit Anti-Windup (Clamp).
 *
 * Drei Instanzen werden in ctrl_loop.c verwendet:
 *   - Stromregler     (schnell, 100 µs)
 *   - Geschwindigkeitsregler (1 ms)
 *   - Positionsregler – nur P-Anteil (10 ms)
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float kp;         /**< Proportionalverstärkung */
    float ki;         /**< Integralverstärkung */
    float out_min;    /**< Ausgangs-Untergrenze (Anti-Windup) */
    float out_max;    /**< Ausgangs-Obergrenze  (Anti-Windup) */
    float integrator; /**< Integrator-Zustand (kein Reset ohne Grund) */
    float dt;         /**< Abtastzeit [s] */
} pi_state_t;

/**
 * @brief PI-Zustand initialisieren.
 * @param s       Zustandsstruktur
 * @param kp      Proportionalverstärkung
 * @param ki      Integralverstärkung
 * @param out_min Ausgangslimit unten
 * @param out_max Ausgangslimit oben
 * @param dt      Abtastzeit [s]
 */
void pi_init(pi_state_t *s, float kp, float ki,
             float out_min, float out_max, float dt);

/**
 * @brief PI-Schritt berechnen.
 * @param s        Zustandsstruktur (wird aktualisiert)
 * @param setpoint Sollwert
 * @param measured Istwert
 * @return         Stellgröße (begrenzt auf [out_min, out_max])
 */
float pi_update(pi_state_t *s, float setpoint, float measured);

/** @brief Integrator zurücksetzen (z.B. beim Aktivieren des Reglers). */
void pi_reset(pi_state_t *s);

/** @brief Koeffizienten aktualisieren ohne Integrator-Reset. */
void pi_set_gains(pi_state_t *s, float kp, float ki);

#ifdef __cplusplus
}
#endif
