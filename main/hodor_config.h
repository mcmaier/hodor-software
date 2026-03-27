/**
 * @file hodor_config.h
 * @brief Zentrale Konstanten für HODOR – Hold/Open Door Operation Regulator.
 *
 * Alle Magic Numbers des Projekts leben hier. Kein Modul darf numerische
 * Literale für Pins, Prioritäten, Stack-Größen oder Timing verwenden.
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * Task-Stack-Größen (Wörter = 4 Byte auf Xtensa LX7)
 * ========================================================================= */
#define HODOR_STACK_CTRL_TASK         3072u
#define HODOR_STACK_MOT_WDG_TASK      1024u
#define HODOR_STACK_SNS_TASK          2048u
#define HODOR_STACK_SM_TASK           2048u
#define HODOR_STACK_DOOR_TASK         2048u
#define HODOR_STACK_IO_TASK           1536u
#define HODOR_STACK_COMM_WIFI_TASK    4096u
#define HODOR_STACK_COMM_MQTT_TASK    4096u
#define HODOR_STACK_COMM_HTTP_TASK    6144u
#define HODOR_STACK_COMM_UART_TASK    2048u
#define HODOR_STACK_CFG_TASK          2048u

/* =========================================================================
 * Task-Prioritäten (0 = idle, 24 = höchste App-Prio; ESP-IDF-Konvention)
 * ========================================================================= */
#define HODOR_PRIO_CTRL_TASK          22
#define HODOR_PRIO_MOT_WDG_TASK       21
#define HODOR_PRIO_SNS_TASK           20
#define HODOR_PRIO_SM_TASK            18
#define HODOR_PRIO_DOOR_TASK          17
#define HODOR_PRIO_IO_TASK            16
#define HODOR_PRIO_COMM_WIFI_TASK     12
#define HODOR_PRIO_COMM_MQTT_TASK     11
#define HODOR_PRIO_COMM_HTTP_TASK     10
#define HODOR_PRIO_COMM_UART_TASK      9
#define HODOR_PRIO_CFG_TASK            8

/* Core-Zuweisung */
#define HODOR_CORE_COMM               0   /* Core 0: WiFi, MQTT, HTTP, UART */
#define HODOR_CORE_CTRL               1   /* Core 1: SM, Regler, Motor, I/O  */

/* =========================================================================
 * Queue-Längen
 * ========================================================================= */
#define HODOR_QLEN_SNS_ISR            8u
#define HODOR_QLEN_SNS_DATA           4u
#define HODOR_QLEN_SM_EVENT          16u
#define HODOR_QLEN_DOOR_EVENT         8u
#define HODOR_QLEN_IO_ISR             8u
#define HODOR_QLEN_CFG_WRITE          8u
#define HODOR_QLEN_COMM_STATUS        4u

/* =========================================================================
 * Timing-Konstanten
 * ========================================================================= */
/** Stromregler-Periode [µs] */
#define HODOR_CTRL_PERIOD_US          100u
/** Geschwindigkeitsregler jede N-te Stromregler-Iteration (→ 1 ms) */
#define HODOR_CTRL_VEL_DIVIDER        10u
/** Positionsregler jede N-te Stromregler-Iteration (→ 10 ms) */
#define HODOR_CTRL_POS_DIVIDER        100u

/** Watchdog-Retrigger-Intervall [ms] – muss << HODOR_WDG_TIMEOUT_MS */
#define HODOR_WDG_RETRIGGER_MS        5u
/** Hardware-Watchdog 74LVC1G123 Timeout [ms] (durch R/C-Glied vorgegeben) */
#define HODOR_WDG_TIMEOUT_MS          200u

/** Maximale Init-Wartezeit in app_main [ms] */
#define HODOR_INIT_TIMEOUT_MS         5000u

/** Standard-Telemetrie-Periode [ms] (überschreibbar via PARAM_UART_STREAM_MS) */
#define HODOR_UART_STREAM_MS_DEFAULT  100u

/** Autoclose-Standardwert [s]; 0 = deaktiviert */
#define HODOR_AUTOCLOSE_S_DEFAULT     0u

/* =========================================================================
 * PWM / LEDC
 * ========================================================================= */
#define HODOR_PWM_FREQ_HZ             20000u
/* Enum-Werte aus driver/ledc.h als Integer – kein Driver-Include nötig.
 * hal_ledc.h/.c inkludiert driver/ledc.h und castet implizit. */
#define HODOR_LEDC_TIMER       0   /* LEDC_TIMER_0    */
#define HODOR_LEDC_CH_PWM_1    0   /* LEDC_CHANNEL_0  */
#define HODOR_LEDC_CH_PWM_2    1   /* LEDC_CHANNEL_1  */
/** 12-Bit-Auflösung → 4096 Stufen bei 20 kHz auf 240-MHz-APB möglich */
#define HODOR_LEDC_RESOLUTION  12  /* LEDC_TIMER_12_BIT */
#define HODOR_LEDC_DUTY_MAX           ((1u << 12) - 1u)   /* 4095 */

/* =========================================================================
 * GPIO-Pinbelegung
 * Quelle: doc/hardware.md – Platzhalter bis PCB-Layout final.
 * ========================================================================= */

/* H-Brücke */
#define HODOR_GPIO_MOT_PWM_1          47
#define HODOR_GPIO_MOT_PWM_2          48
#define HODOR_GPIO_MOT_EN             45
#define HODOR_GPIO_MOT_NFAULT         46

/* H-Brücken-Treiber MC33HB2002 (SPI) */
#define HODOR_GPIO_DRV_SCLK           12
#define HODOR_GPIO_DRV_MISO           11
#define HODOR_GPIO_DRV_MOSI           13
#define HODOR_GPIO_DRV_CS             10

/* Encoder (Quadratur via PCNT) / Endschalter */
#define HODOR_GPIO_ENC_A              35
#define HODOR_GPIO_ENC_B              36

/* Potentialfreie Steuereingänge */
#define HODOR_GPIO_IN_1               17
#define HODOR_GPIO_IN_2               18
#define HODOR_GPIO_IN_3               39

/* Schaltbares Relais */
#define HODOR_GPIO_OUT_1               3

/* Hardware-Watchdog 74LVC1G123 */
#define HODOR_GPIO_WDG_TRIG           42   /* Retrigger-Puls (OUT) */
#define HODOR_GPIO_WDG_NCLR           41   /* /CLR: Low = sofortiger Reset (OUT) */

/* Status-LEDs */
#define HODOR_GPIO_LED_STATUS         16
#define HODOR_GPIO_LED_ACTIVE         15
#define HODOR_GPIO_LED_CONN           14

/* UART Debug (UART0) */
#define HODOR_GPIO_UART_TX            43
#define HODOR_GPIO_UART_RX            44

/* =========================================================================
 * Strommessung – ACS725LLCTR-05AB
 * ========================================================================= */
#define HODOR_ACS725_SENSITIVITY_V_PER_A   0.400f   /* 400 mV/A */
#define HODOR_ACS725_MIDPOINT_V            1.500f   /* ~VCC/2 bei 0 A, 3,3 V */
#define HODOR_ACS725_VCC_V                 3.300f
#define HODOR_ACS725_MAX_A                 5.0f     /* Messbereich ±5 A */

/** ADC-Referenzspannung [V] (nach Kalibrierung) */
#define HODOR_ADC_VREF_MV                  3300u

/* =========================================================================
 * Regler-Sicherheitslimits
 * ========================================================================= */
#define HODOR_CURRENT_HW_MAX_A        5.0f
#define HODOR_PWM_DUTY_MAX_PCT        100.0f
#define HODOR_PWM_DUTY_MIN_PCT        (-100.0f)

/* =========================================================================
 * NVS
 * ========================================================================= */
#define HODOR_NVS_NAMESPACE           "hodor"

/* =========================================================================
 * Sonstiges
 * ========================================================================= */
#define HODOR_UART_BAUD               115200u

/** Anzahl ADC-Oversampling-Messungen pro Reglertakt */
#define HODOR_ADC_OVERSAMPLE          4u

/** Blockier-Erkennung: Strom überschreitet Schwelle für diese Dauer [ms] */
#define HODOR_BLOCK_DETECT_MS         200u

#ifdef __cplusplus
}
#endif
