#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <string.h>
#include <stdio.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

/* ==================== MMC5983MA 定义 ==================== */
#define MMC5983MA_ADDR          0x30
#define MMC5983MA_REG_XOUT0     0x00
#define MMC5983MA_REG_PRODUCT_ID 0x2F
#define MMC5983MA_CTRL0_TM      0x01
#define MMC5983MA_CTRL0_SET     0x08
#define MMC5983MA_CTRL1_BW_100HZ 0x00

/* ==================== LoRa SX1276 定义 ==================== */
#define LORA_FREQ_HZ  915000000UL
#define TX_POWER      14

static struct spi_dt_spec lora_spi =
    SPI_DT_SPEC_GET(DT_NODELABEL(rfm95),
                    SPI_WORD_SET(8) | SPI_TRANSFER_MSB, 0);

/* ==================== SX1276 函数 ==================== */
static void sx1276_write_reg(uint8_t reg, uint8_t val)
{
    uint8_t tx_buf[2] = { reg | 0x80, val };
    struct spi_buf tx = { .buf = tx_buf, .len = 2 };
    const struct spi_buf_set txs = { .buffers = &tx, .count = 1 };
    spi_write_dt(&lora_spi, &txs);
}

static int sx1276_read_reg(uint8_t reg, uint8_t *val)
{
    uint8_t tx_buf[2] = { reg & 0x7F, 0x00 };
    uint8_t rx_buf[2] = { 0 };
    struct spi_buf tx = { .buf = tx_buf, .len = 2 };
    struct spi_buf rx = { .buf = rx_buf, .len = 2 };
    const struct spi_buf_set tx_set = { .buffers = &tx, .count = 1 };
    const struct spi_buf_set rx_set = { .buffers = &rx, .count = 1 };
    int ret = spi_transceive_dt(&lora_spi, &tx_set, &rx_set);
    if (ret == 0) *val = rx_buf[1];
    return ret;
}

static void sx1276_write_fifo(const uint8_t *data, uint8_t len)
{
    uint8_t buf[1 + 64];
    buf[0] = 0x80;
    memcpy(&buf[1], data, len);
    struct spi_buf tx = { .buf = buf, .len = len + 1 };
    const struct spi_buf_set txs = { .buffers = &tx, .count = 1 };
    spi_write_dt(&lora_spi, &txs);
}

static void sx1276_set_freq(uint32_t freq)
{
    uint64_t frf = ((uint64_t)freq << 19) / 32000000;
    sx1276_write_reg(0x06, (frf >> 16) & 0xFF);
    sx1276_write_reg(0x07, (frf >> 8) & 0xFF);
    sx1276_write_reg(0x08, (frf) & 0xFF);
}

static void sx1276_init_lora(void)
{
    sx1276_write_reg(0x01, 0x80);
    k_msleep(5);
    sx1276_write_reg(0x01, 0x81);
    sx1276_set_freq(LORA_FREQ_HZ);
    sx1276_write_reg(0x1D, 0x72);
    sx1276_write_reg(0x1E, 0x74);
    sx1276_write_reg(0x26, 0x04);
    sx1276_write_reg(0x39, 0x34);
    sx1276_write_reg(0x09, 0x8F);
    sx1276_write_reg(0x12, 0xFF);
    LOG_INF("LoRa initialized at 915 MHz");
}

static void sx1276_send_packet(const uint8_t *data, uint8_t len)
{
    if (len == 0 || len > 64) return;
    
    uint8_t irq = 0;
    
    sx1276_write_reg(0x01, 0x81);
    k_msleep(2);
    sx1276_write_reg(0x12, 0xFF);
    
    sx1276_set_freq(LORA_FREQ_HZ);
    sx1276_write_reg(0x1D, 0x72);
    sx1276_write_reg(0x1E, 0x74);
    sx1276_write_reg(0x26, 0x04);
    sx1276_write_reg(0x39, 0x34);
    sx1276_write_reg(0x09, 0x8F);
    sx1276_write_reg(0x40, 0x40);
    
    sx1276_write_reg(0x0E, 0x80);
    sx1276_write_reg(0x0D, 0x80);
    sx1276_write_fifo(data, len);
    sx1276_write_reg(0x22, len);
    
    sx1276_write_reg(0x01, 0x83);
    
    for (int i = 0; i < 1000; i++) {
        sx1276_read_reg(0x12, &irq);
        if (irq & 0x08) break;
        k_busy_wait(1000);
    }
    
    if (irq & 0x08) {
        LOG_INF("TX Done");
    } else {
        LOG_WRN("TX Timeout");
    }
    
    sx1276_write_reg(0x12, 0x08);
    sx1276_write_reg(0x01, 0x81);
}

/* ==================== MMC5983MA 函数 ==================== */
static int mmc5983ma_init(const struct device *i2c_dev)
{
    uint8_t product_id;
    uint8_t reg = MMC5983MA_REG_PRODUCT_ID;
    
    int ret = i2c_write_read(i2c_dev, MMC5983MA_ADDR, &reg, 1, &product_id, 1);
    if (ret != 0 || product_id != 0x30) {
        LOG_ERR("MMC5983MA init failed");
        return -1;
    }
    
    uint8_t cmd[2] = {0x09, MMC5983MA_CTRL0_SET};
    i2c_write(i2c_dev, cmd, 2, MMC5983MA_ADDR);
    k_msleep(1);
    
    cmd[0] = 0x0A;
    cmd[1] = MMC5983MA_CTRL1_BW_100HZ;
    i2c_write(i2c_dev, cmd, 2, MMC5983MA_ADDR);
    
    LOG_INF("MMC5983MA initialized (ID: 0x%02X)", product_id);
    return 0;
}

static int mmc5983ma_read_mag(const struct device *i2c_dev, 
                              int32_t *x, int32_t *y, int32_t *z)
{
    uint8_t data[7];
    uint8_t reg = MMC5983MA_REG_XOUT0;
    
    uint8_t cmd[2] = {0x09, MMC5983MA_CTRL0_TM};
    int ret = i2c_write(i2c_dev, cmd, 2, MMC5983MA_ADDR);
    if (ret != 0) return ret;
    
    k_msleep(10);
    
    ret = i2c_write_read(i2c_dev, MMC5983MA_ADDR, &reg, 1, data, 7);
    if (ret != 0) return ret;
    
    *x = ((uint32_t)data[0] << 10) | ((uint32_t)data[1] << 2) | ((data[6] >> 6) & 0x03);
    *y = ((uint32_t)data[2] << 10) | ((uint32_t)data[3] << 2) | ((data[6] >> 4) & 0x03);
    *z = ((uint32_t)data[4] << 10) | ((uint32_t)data[5] << 2) | ((data[6] >> 2) & 0x03);
    
    *x -= 131072;
    *y -= 131072;
    *z -= 131072;
    
    return 0;
}

/* ==================== 主函数 ==================== */
void main(void)
{
    LOG_INF("=== Magnetometer + LoRa TX System ===");
    
    // 初始化 I2C (磁力计)
    const struct device *i2c_dev = DEVICE_DT_GET(DT_NODELABEL(i2c1));
    if (!device_is_ready(i2c_dev)) {
        LOG_ERR("I2C not ready!");
        return;
    }
    
    // 初始化 SPI (LoRa)
    if (!device_is_ready(lora_spi.bus)) {
        LOG_ERR("SPI not ready!");
        return;
    }
    
    // 初始化传感器
    if (mmc5983ma_init(i2c_dev) != 0) {
        LOG_ERR("Magnetometer init failed");
        return;
    }
    
    sx1276_init_lora();
    
    uint32_t packet_count = 0;
    
    while (1) {
        int32_t x, y, z;
        
        // 读取磁场数据
        if (mmc5983ma_read_mag(i2c_dev, &x, &y, &z) == 0) {
            float x_g = x * 0.0000625f;
            float y_g = y * 0.0000625f;
            float z_g = z * 0.0000625f;
            float mag = sqrtf(x_g*x_g + y_g*y_g + z_g*z_g);
            
            LOG_INF("Mag: X=%.3f Y=%.3f Z=%.3f |M|=%.3f G",
                    (double)x_g, (double)y_g, (double)z_g, (double)mag);
            
            // 格式化为字符串并发送
            char message[64];
            snprintf(message, sizeof(message),
                     "#%lu X:%.2f Y:%.2f Z:%.2f M:%.2f",
                     (unsigned long)packet_count,
                     (double)x_g, (double)y_g, (double)z_g, (double)mag);
            
            LOG_INF("Sending: %s", message);
            sx1276_send_packet((uint8_t*)message, strlen(message));
            
            packet_count++;
        } else {
            LOG_ERR("Mag read failed");
        }
        
        k_sleep(K_SECONDS(5));  // 每5秒发送一次
    }
}