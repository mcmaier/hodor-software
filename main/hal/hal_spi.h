/**
 * @file hal_spi.h
 * @brief HAL – SPI-Master für MC33HB2002-Treiberkonfiguration.
 */

#pragma once

#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief SPI-Master-Bus und Device initialisieren.
 *        Pinzuweisung aus hodor_config.h (DRV_SCLK/MISO/MOSI/CS).
 */
esp_err_t hal_spi_init(void);

/**
 * @brief SPI-Transaktion (Full-Duplex, 16-Bit).
 * @param tx_data  Sendewort
 * @param rx_data  Empfangswort (darf NULL sein wenn nicht benötigt)
 */
esp_err_t hal_spi_transfer16(uint16_t tx_data, uint16_t *rx_data);

/** @brief SPI-Bus freigeben. */
void hal_spi_deinit(void);

#ifdef __cplusplus
}
#endif
