/**
 * @file hal_gpio.h
 * @brief HAL – GPIO-Abstraktion für HODOR.
 *
 * Dünne Schicht über esp_rom_gpio / gpio_config. Alle Pin-Nummern kommen
 * aus hodor_config.h – niemals direkte Literale in Aufrufen.
 */

#pragma once

#include "esp_err.h"
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Alle Projekt-GPIOs initialisieren (Richtung, Pull-ups, ISR-Dienst).
 *        Muss vor allen anderen Modul-Inits aufgerufen werden.
 */
esp_err_t hal_gpio_init_all(void);

/**
 * @brief Einzelnen Ausgangs-Pin konfigurieren.
 * @param pin      GPIO-Nummer
 * @param initial  Initialer Pegel (0 oder 1)
 */
esp_err_t hal_gpio_init_output(int pin, int initial);

/**
 * @brief Einzelnen Eingangs-Pin konfigurieren.
 * @param pin       GPIO-Nummer
 * @param pull_up   true = interner Pull-up aktiv
 * @param pull_down true = interner Pull-down aktiv
 */
esp_err_t hal_gpio_init_input(int pin, bool pull_up, bool pull_down);

/** @brief GPIO-Pegel setzen (nur Output-Pins). */
void hal_gpio_set(int pin, int level);

/** @brief GPIO-Pegel lesen. */
int  hal_gpio_get(int pin);

#ifdef __cplusplus
}
#endif
