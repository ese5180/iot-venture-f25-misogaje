#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>
#include <string.h>
#include <stdio.h>

LOG_MODULE_REGISTER(misonode, LOG_LEVEL_INF);

/* ---------- RFM69 registers (subset) ---------- */
#define R_REG_FIFO              0x00
#define R_REG_OPMODE            0x01
#define R_REG_DATAMODUL         0x02
#define R_REG_BITRATEMSB        0x03
#define R_REG_BITRATELSB        0x04
#define R_REG_FDEVMSB           0x05
#define R_REG_FDEVLSB           0x06
#define R_REG_FRFMSB            0x07
#define R_REG_FRFMID            0x08
#define R_REG_FRFLSB            0x09
#define RFM69_REG_VERSION       0x10
#define R_REG_PALEVEL           0x11
#define R_REG_OCP               0x13
#define R_REG_LNA               0x18
#define R_REG_RXBW              0x19
#define R_REG_DIOMAPPING1       0x25
#define R_REG_IRQFLAGS1         0x27
#define R_REG_IRQFLAGS2         0x28
#define R_REG_RSSITHRESH        0x29
#define R_REG_PREAMBLEMSB       0x2C
#define R_REG_PREAMBLELSB       0x2D
#define R_REG_SYNCCONFIG        0x2E
#define R_REG_SYNCVALUE1        0x2F
#define R_REG_SYNCVALUE2        0x30
#define R_REG_PACKETCONFIG1     0x37
#define R_REG_PAYLOADLENGTH     0x38
#define R_REG_FIFOTHRESH        0x3C
#define R_REG_PACKETCONFIG2     0x3D

/* OpMode bits */
#define OPMODE_SEQUENCER_ON     0x80
#define OPMODE_LISTEN_OFF       0x00
#define OPMODE_MODE_SLEEP       0x00
#define OPMODE_MODE_STDBY       0x04
#define OPMODE_MODE_FS          0x08
#define OPMODE_MODE_TX          0x0C
#define OPMODE_MODE_RX          0x10

/* Flags */
#define IRQ1_MODEREADY          BIT(7)
#define IRQ2_PAYLOADREADY       BIT(2)
#define IRQ2_FIFOOVERRUN        BIT(4)

/* ---------- Board wiring (nRF7002 DK) ----------
 * SPI bus: Arduino header SPI => DT node arduino_spi
 * CS:     D10 = P1.12
 * RESET:  D9  = P1.10  (ACTIVE HIGH)
 */
static const struct device *spi_dev = DEVICE_DT_GET(DT_NODELABEL(arduino_spi));

static const struct gpio_dt_spec cs_gpio = {
    .port = DEVICE_DT_GET(DT_NODELABEL(gpio1)),
    .pin = 12,
    .dt_flags = GPIO_ACTIVE_LOW
};

static const struct gpio_dt_spec reset_gpio = {
    .port = DEVICE_DT_GET(DT_NODELABEL(gpio1)),
    .pin = 10,
    .dt_flags = GPIO_ACTIVE_HIGH
};

static struct spi_config spi_cfg = {
    .frequency = 1000000,
    .operation = SPI_OP_MODE_MASTER | SPI_WORD_SET(8) | SPI_TRANSFER_MSB, /* mode 0 */
    .slave = 0,
    .cs = { .gpio = cs_gpio, .delay = 0 },
};

/* ---------- SPI helpers ---------- */
static uint8_t rfm69_read_reg(uint8_t reg)
{
    uint8_t tx_buf[2] = { (uint8_t)(reg & 0x7F), 0x00 };
    uint8_t rx_buf[2] = {0};

    const struct spi_buf tx = { .buf = tx_buf, .len = 2 };
    const struct spi_buf rx = { .buf = rx_buf, .len = 2 };
    const struct spi_buf_set tx_set = { .buffers = &tx, .count = 1 };
    const struct spi_buf_set rx_set = { .buffers = &rx, .count = 1 };

    int ret = spi_transceive(spi_dev, &spi_cfg, &tx_set, &rx_set);
    if (ret < 0) {
        LOG_ERR("SPI transceive failed: %d", ret);
        return 0xFF;
    }
    return rx_buf[1];
}

static int rfm69_write_reg(uint8_t reg, uint8_t val)
{
    uint8_t tx_buf[2] = { (uint8_t)(reg | 0x80), val };
    const struct spi_buf tx = { .buf = tx_buf, .len = 2 };
    const struct spi_buf_set tx_set = { .buffers = &tx, .count = 1 };
    int ret = spi_write(spi_dev, &spi_cfg, &tx_set);
    if (ret < 0) LOG_ERR("SPI write failed %d (reg 0x%02x)", ret, reg);
    return ret;
}

/* ---------- Mode control & reset ---------- */
static void rfm69_write_opmode(uint8_t mode_bits)
{
    uint8_t v = OPMODE_SEQUENCER_ON | OPMODE_LISTEN_OFF | (mode_bits & 0x1C);
    rfm69_write_reg(R_REG_OPMODE, v);
}

static int rfm69_wait_modeready(int32_t timeout_us)
{
    while (timeout_us > 0) {
        uint8_t f1 = rfm69_read_reg(R_REG_IRQFLAGS1);
        if (f1 & IRQ1_MODEREADY) return 0;
        k_busy_wait(100);
        timeout_us -= 100;
    }
    return -ETIMEDOUT;
}

static int rfm69_set_mode_blocking(uint8_t mode_bits)
{
    rfm69_write_opmode(mode_bits);
    if (mode_bits == OPMODE_MODE_SLEEP) {
        k_msleep(2);
        return 0;
    }
    return rfm69_wait_modeready(50000);
}

static void rfm69_reset(void)
{
    gpio_pin_set_dt(&reset_gpio, 1);
    k_busy_wait(200);
    gpio_pin_set_dt(&reset_gpio, 0);
    k_msleep(15);
}

static int rfm69_wake(void)
{
    if (rfm69_set_mode_blocking(OPMODE_MODE_SLEEP) < 0) return -EIO;
    k_msleep(2);
    if (rfm69_set_mode_blocking(OPMODE_MODE_STDBY) < 0) return -EIO;
    return 0;
}

/* ---------- Radio config (same as TX except DIO map + RSSI) ---------- */
static int rfm69_init(void)
{
    if (rfm69_wake() < 0) {
        LOG_ERR("Wake sequence failed");
        return -EIO;
    }

    /* PHY identical to TX */
    rfm69_write_reg(R_REG_DATAMODUL, 0x00);      /* packet, FSK, no shaping */
    rfm69_write_reg(R_REG_BITRATEMSB, 0x02);     /* ~55.556 kbps */
    rfm69_write_reg(R_REG_BITRATELSB, 0x40);
    rfm69_write_reg(R_REG_FDEVMSB,   0x03);      /* ~50 kHz */
    rfm69_write_reg(R_REG_FDEVLSB,   0x33);
    rfm69_write_reg(R_REG_FRFMSB,    0xE4);      /* 915.000 MHz */
    rfm69_write_reg(R_REG_FRFMID,    0xC0);
    rfm69_write_reg(R_REG_FRFLSB,    0x00);
    rfm69_write_reg(R_REG_PALEVEL,   0x80 | 0x1F); /* harmless in RX */
    rfm69_write_reg(R_REG_OCP,       0x1A);
    rfm69_write_reg(R_REG_LNA,       0x88);
    rfm69_write_reg(R_REG_RXBW,      0x55);
    rfm69_write_reg(R_REG_PREAMBLEMSB,0x00);
    rfm69_write_reg(R_REG_PREAMBLELSB,0x03);
    rfm69_write_reg(R_REG_SYNCCONFIG, 0x88);     /* Sync on, 2 bytes */
    rfm69_write_reg(R_REG_SYNCVALUE1, 0x2D);
    rfm69_write_reg(R_REG_SYNCVALUE2, 0xD4);
    rfm69_write_reg(R_REG_PACKETCONFIG1, 0xD0);  /* var-len + DC-free + CRC */
    rfm69_write_reg(R_REG_PAYLOADLENGTH, 64);    /* max for var-len */
    rfm69_write_reg(R_REG_FIFOTHRESH,   0x80 | 15);
    rfm69_write_reg(R_REG_PACKETCONFIG2,0x02);   /* AES off */

    /* --- RX-specific tweaks --- */
    rfm69_write_reg(R_REG_DIOMAPPING1, 0x40);    /* DIO0: PayloadReady (RX) */
    rfm69_write_reg(R_REG_RSSITHRESH,  0xE4);    /* ~-90 dBm */

    if (rfm69_set_mode_blocking(OPMODE_MODE_STDBY) < 0) {
        LOG_ERR("Standby ModeReady timeout after config");
        return -EIO;
    }
    return 0;
}

/* Receive one variable-length packet (<=64 bytes) with timeout (ms) */
static int rfm69_recv(uint8_t *out, uint8_t *out_len, int32_t timeout_ms)
{
    if (!out || !out_len) return -EINVAL;

    (void)rfm69_read_reg(R_REG_IRQFLAGS1);
    (void)rfm69_read_reg(R_REG_IRQFLAGS2);

    if (rfm69_set_mode_blocking(OPMODE_MODE_RX) < 0) return -EIO;

    int32_t left = timeout_ms;
    while (left > 0) {
        uint8_t f2 = rfm69_read_reg(R_REG_IRQFLAGS2);
        if (f2 & IRQ2_PAYLOADREADY) {
            /* var-len: first byte is length */
            uint8_t len = rfm69_read_reg(R_REG_FIFO);
            if (len == 0 || len > 64) goto restart;

            for (uint8_t i = 0; i < len; i++) {
                out[i] = rfm69_read_reg(R_REG_FIFO);
            }
            *out_len = len;

            (void)rfm69_set_mode_blocking(OPMODE_MODE_STDBY);
            return 0;
        }
        k_sleep(K_MSEC(1));
        left -= 1;
    }

    (void)rfm69_set_mode_blocking(OPMODE_MODE_STDBY);
    return -ETIMEDOUT;

restart:
    /* Restart RX packet handler if junk length */
    uint8_t pc2 = rfm69_read_reg(R_REG_PACKETCONFIG2);
    rfm69_write_reg(R_REG_PACKETCONFIG2, pc2 | 0x04); /* RestartRx */
    (void)rfm69_set_mode_blocking(OPMODE_MODE_STDBY);
    return -EIO;
}

/* ---------- Main ---------- */
int main(void)
{
    LOG_INF("Misonode RX starting...");

    if (!device_is_ready(spi_dev)) {
        LOG_ERR("SPI device not ready");
        return -1;
    }
    if (!device_is_ready(cs_gpio.port) || !device_is_ready(reset_gpio.port)) {
        LOG_ERR("GPIO ports not ready");
        return -1;
    }
    if (gpio_pin_configure_dt(&reset_gpio, GPIO_OUTPUT_INACTIVE) < 0) {
        LOG_ERR("Reset GPIO configure failed");
        return -1;
    }

    rfm69_reset();

    uint8_t ver = rfm69_read_reg(RFM69_REG_VERSION);
    LOG_INF("RFM69 Version: 0x%02x (expect 0x24)", ver);
    if (ver != 0x24) {
        LOG_ERR("Unexpected version; check CS/RESET wiring & overlay");
        return -1;
    }

    if (rfm69_init() < 0) {
        uint8_t op  = rfm69_read_reg(R_REG_OPMODE);
        uint8_t f1  = rfm69_read_reg(R_REG_IRQFLAGS1);
        uint8_t f2  = rfm69_read_reg(R_REG_IRQFLAGS2);
        LOG_ERR("RFM69 init failed. OpMode=0x%02x IRQ1=0x%02x IRQ2=0x%02x", op, f1, f2);
        return -1;
    }
    LOG_INF("RFM69 init OK, listening...");

    while (1) {
        uint8_t buf[64], n = 0;
        int rc = rfm69_recv(buf, &n, 1000);
        if (rc == 0) {
            /* ensure printable for the log */
            if (n < sizeof(buf)) buf[n] = 0;
            LOG_INF("RX: len=%u \"%s\"", n, buf);
        } else if (rc != -ETIMEDOUT) {
            LOG_WRN("RX error: %d", rc);
        }
        /* keep listening; short pause optional */
    }
}
