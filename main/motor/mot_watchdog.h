/**
 * @file mot_watchdog.h
 * @brief Hardware-Watchdog-Steuerung (74LVC1G123).
 *
 * Der Watchdog-Trigger läuft NUR wenn Motorbetrieb beabsichtigt ist.
 * Aktivierung: mot_wdg_enable() – synchronisiert mit SYS_ACTIVE-Entry.
 * Deaktivierung: mot_wdg_disable() – sofortiges Hardware-Clear via /CLR.
 *
 * Im inaktiven Zustand ist WDG_NCLR = LOW (74LVC1G123 in Reset):
 *   → /Q = HIGH → H-Brücke disabled (sicherer Zustand).
 */

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Watchdog-Modul initialisieren.
 *        WDG_TRIG = LOW, WDG_NCLR = LOW (Watchdog in Reset, sicherer Zustand).
 *        Erstellt interne Timer-Semaphore – kein Triggern bis mot_wdg_enable().
 */
esp_err_t mot_wdg_init(void);

/**
 * @brief Watchdog aktivieren und Retrigger starten.
 *        Setzt WDG_NCLR = HIGH (Watchdog freigeben), dann startet der
 *        HODOR_WDG_RETRIGGER_MS-Timer.
 *        Muss von sm_task beim Eintritt in SYS_ACTIVE aufgerufen werden.
 */
esp_err_t mot_wdg_enable(void);

/**
 * @brief Watchdog deaktivieren.
 *        Stoppt Retrigger-Pulse und setzt WDG_NCLR = LOW.
 *        → 74LVC1G123 sofort zurückgesetzt → H-Brücken-Pfad hardwareseitig unterbrochen.
 *        Muss bei jedem Verlassen von SYS_ACTIVE aufgerufen werden.
 */
void mot_wdg_disable(void);

/**
 * @brief mot_wdg_task – periodischer Watchdog-Retrigger (Core 1, Prio 21).
 *        Blockiert auf Semaphor; löst Puls nur aus wenn mot_wdg_enable() aktiv.
 */
void mot_wdg_task_func(void *arg);

#ifdef __cplusplus
}
#endif
