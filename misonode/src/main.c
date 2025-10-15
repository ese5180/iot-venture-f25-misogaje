#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(misonode, LOG_LEVEL_INF);

#define RFM69_REG_VERSION 0x10

static const struct device *spi_dev = DEVICE_DT_GET(DT_NODELABEL(arduino_spi));

// CS pin: D10 = P1.12
static const struct gpio_dt_spec cs_gpio = {
    .port = DEVICE_DT_GET(DT_NODELABEL(gpio1)),
    .pin = 12,
    .dt_flags = GPIO_ACTIVE_LOW
};

// Reset pin: D9 = P1.10
static const struct gpio_dt_spec reset_gpio = {
    .port = DEVICE_DT_GET(DT_NODELABEL(gpio1)),
    .pin = 10,
    .dt_flags = GPIO_ACTIVE_LOW
};

static struct spi_config spi_cfg = {
    .frequency = 1000000,
    .operation = SPI_OP_MODE_MASTER | SPI_WORD_SET(8) | SPI_TRANSFER_MSB,
    .slave = 0,
    .cs = {
        .gpio = cs_gpio,
        .delay = 0,
    },
};

static void rfm69_reset(void)
{
    LOG_INF("Resetting RFM69...");
    gpio_pin_set_dt(&reset_gpio, 1);  // Assert reset (active low)
    k_msleep(10);
    gpio_pin_set_dt(&reset_gpio, 0);  // Release reset
    k_msleep(10);
    LOG_INF("Reset complete");
}

static uint8_t rfm69_read_reg(uint8_t reg)
{
    uint8_t tx_buf[2] = {reg & 0x7F, 0x00};
    uint8_t rx_buf[2] = {0};
    
    const struct spi_buf tx = {.buf = tx_buf, .len = 2};
    const struct spi_buf rx = {.buf = rx_buf, .len = 2};
    const struct spi_buf_set tx_set = {.buffers = &tx, .count = 1};
    const struct spi_buf_set rx_set = {.buffers = &rx, .count = 1};
    
    int ret = spi_transceive(spi_dev, &spi_cfg, &tx_set, &rx_set);
    if (ret < 0) {
        LOG_ERR("SPI transceive failed: %d", ret);
    }
    
    return rx_buf[1];
}

static void rfm69_write_reg(uint8_t reg, uint8_t value)
{
    uint8_t tx_buf[2] = {reg | 0x80, value};
    
    const struct spi_buf tx = {.buf = tx_buf, .len = 2};
    const struct spi_buf_set tx_set = {.buffers = &tx, .count = 1};
    
    int ret = spi_write(spi_dev, &spi_cfg, &tx_set);
    if (ret < 0) {
        LOG_ERR("SPI write failed: %d", ret);
    }
}

int main(void)
{
    LOG_INF("Misonode starting...");
    
    // Check SPI device
    if (!device_is_ready(spi_dev)) {
        LOG_ERR("SPI device not ready!");
        return -1;
    }
    LOG_INF("SPI device ready");
    
    // Configure CS pin
    if (!device_is_ready(cs_gpio.port)) {
        LOG_ERR("CS GPIO not ready!");
        return -1;
    }
    int ret = gpio_pin_configure_dt(&cs_gpio, GPIO_OUTPUT_INACTIVE);
    if (ret < 0) {
        LOG_ERR("CS GPIO configure failed: %d", ret);
        return -1;
    }
    LOG_INF("CS GPIO configured (P1.12 / D10)");
    
    // Configure reset pin
    if (!device_is_ready(reset_gpio.port)) {
        LOG_ERR("Reset GPIO not ready!");
        return -1;
    }
    ret = gpio_pin_configure_dt(&reset_gpio, GPIO_OUTPUT_INACTIVE);
    if (ret < 0) {
        LOG_ERR("Reset GPIO configure failed: %d", ret);
        return -1;
    }
    LOG_INF("Reset GPIO configured (P1.10 / D9)");
    
    // Reset the RFM69
    rfm69_reset();
    
    // Try reading version multiple times
    for (int i = 0; i < 5; i++) {
        uint8_t version = rfm69_read_reg(RFM69_REG_VERSION);
        LOG_INF("Attempt %d - RFM69 Version: 0x%02x (should be 0x24)", i+1, version);
        
        if (version == 0x24) {
            LOG_INF("RFM69HCW detected successfully!");
            break;
        }
        k_msleep(100);
    }
    
    while (1) {
        k_sleep(K_SECONDS(5));
    }
    
    return 0;
}