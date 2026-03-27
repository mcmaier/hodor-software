/**
 * @file hal_spi.c
 * @brief HAL – SPI-Master für MC33HB2002 H-Brücken-Treiber.
 */

#include "hal_spi.h"
#include "hodor_config.h"
#include "driver/spi_master.h"
#include "esp_log.h"

static const char *TAG = "hal_spi";

#define HODOR_SPI_HOST   SPI2_HOST
#define HODOR_SPI_CLK_HZ 1000000   /* 1 MHz – MC33HB2002 max. 5 MHz */

static spi_device_handle_t s_spi_dev = NULL;

esp_err_t hal_spi_init(void)
{
    esp_err_t ret;

    spi_bus_config_t bus_cfg = {
        .mosi_io_num     = HODOR_GPIO_DRV_MOSI,
        .miso_io_num     = HODOR_GPIO_DRV_MISO,
        .sclk_io_num     = HODOR_GPIO_DRV_SCLK,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = 4,
    };
    ret = spi_bus_initialize(HODOR_SPI_HOST, &bus_cfg, SPI_DMA_DISABLED);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_initialize: %s", esp_err_to_name(ret));
        return ret;
    }

    spi_device_interface_config_t dev_cfg = {
        .clock_speed_hz = HODOR_SPI_CLK_HZ,
        .mode           = 1,          /* CPOL=0, CPHA=1 – MC33HB2002 */
        .spics_io_num   = HODOR_GPIO_DRV_CS,
        .queue_size     = 1,
    };
    ret = spi_bus_add_device(HODOR_SPI_HOST, &dev_cfg, &s_spi_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_add_device: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "SPI Master initialisiert (%d Hz)", HODOR_SPI_CLK_HZ);
    return ESP_OK;
}

esp_err_t hal_spi_transfer16(uint16_t tx_data, uint16_t *rx_data)
{
    uint16_t rx_buf = 0;
    spi_transaction_t t = {
        .length    = 16,
        .tx_buffer = &tx_data,
        .rx_buffer = &rx_buf,
    };
    esp_err_t ret = spi_device_polling_transmit(s_spi_dev, &t);
    if (ret == ESP_OK && rx_data) {
        *rx_data = rx_buf;
    }
    return ret;
}

void hal_spi_deinit(void)
{
    if (s_spi_dev) {
        spi_bus_remove_device(s_spi_dev);
        s_spi_dev = NULL;
    }
    spi_bus_free(HODOR_SPI_HOST);
}
