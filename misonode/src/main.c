#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(misonode, LOG_LEVEL_INF);

#define RFM69_REG_VERSION 0x10

static const struct device *spi_dev = DEVICE_DT_GET(DT_NODELABEL(arduino_spi));
static struct spi_config spi_cfg = {
    .frequency = 1000000,
    .operation = SPI_OP_MODE_MASTER | SPI_WORD_SET(8) | SPI_TRANSFER_MSB,
    .slave = 0,
};

static uint8_t rfm69_read_reg(uint8_t reg)
{
    uint8_t tx_buf[2] = {reg & 0x7F, 0x00};
    uint8_t rx_buf[2] = {0};
    
    const struct spi_buf tx = {.buf = tx_buf, .len = 2};
    const struct spi_buf rx = {.buf = rx_buf, .len = 2};
    const struct spi_buf_set tx_set = {.buffers = &tx, .count = 1};
    const struct spi_buf_set rx_set = {.buffers = &rx, .count = 1};
    
    spi_transceive(spi_dev, &spi_cfg, &tx_set, &rx_set);
    return rx_buf[1];
}

int main(void)
{
    LOG_INF("Misonode starting...");
    
    if (!device_is_ready(spi_dev)) {
        LOG_ERR("SPI device not ready!");
        return -1;
    }
    
    LOG_INF("SPI device ready");
    
    // Test reading version register
    uint8_t version = rfm69_read_reg(RFM69_REG_VERSION);
    LOG_INF("RFM69 Version: 0x%02x (should be 0x24)", version);
    
    if (version == 0x24) {
        LOG_INF("RFM69HCW detected successfully!");
    } else {
        LOG_ERR("Failed to detect RFM69HCW. Check wiring!");
    }
    
    while (1) {
        k_sleep(K_SECONDS(5));
    }
    
    return 0;
}