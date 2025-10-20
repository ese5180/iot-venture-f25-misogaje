// misonode_rfm69_rx.c — nRF7002 DK + RFM69HCW (RX) — same style as your TX (no overlay)
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <string.h>

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
#define R_REG_PALEVEL           0x11
#define R_REG_OCP               0x13
#define R_REG_LNA               0x18
#define R_REG_RXBW              0x19
#define R_REG_RSSICONFIG        0x23
#define R_REG_RSSIVALUE         0x24
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
#define RFM69_REG_VERSION       0x10

/* OpMode bits */
#define OPMODE_SEQUENCER_ON     0x80
#define OPMODE_LISTEN_OFF       0x00
#define OPMODE_MODE_SLEEP       0x00
#define OPMODE_MODE_STDBY       0x04
#define OPMODE_MODE_RX          0x10

/* IRQ bits */
#define IRQ1_MODEREADY          BIT(7)
#define IRQ2_PAYLOADREADY       BIT(2)
#define IRQ2_FIFOOVERRUN        BIT(4)

/* ---------- Board wiring (nRF7002 DK) ----------
 * SPI:   Arduino header SPI node
 * CS:    D10 = P1.12 (active low)
 * RESET: D9  = P1.10 (ACTIVE HIGH)
 */
static const struct device *spi_dev = DEVICE_DT_GET(DT_NODELABEL(arduino_spi));

static const struct gpio_dt_spec cs_gpio = {
    .port = DEVICE_DT_GET(DT_NODELABEL(gpio1)),
    .pin = 12,                   // D10
    .dt_flags = GPIO_ACTIVE_LOW
};
static const struct gpio_dt_spec reset_gpio = {
    .port = DEVICE_DT_GET(DT_NODELABEL(gpio1)),
    .pin = 10,                   // D9
    .dt_flags = GPIO_ACTIVE_HIGH // RFM69 reset is ACTIVE-HIGH
};

static struct spi_config spi_cfg = {
    .frequency = 1000000,
    .operation = SPI_OP_MODE_MASTER | SPI_WORD_SET(8) | SPI_TRANSFER_MSB, // mode 0
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
    if (ret < 0) { LOG_ERR("SPI transceive failed: %d", ret); return 0xFF; }
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

static int rfm69_read_burst(uint8_t reg, uint8_t *buf, size_t len)
{
    if (!buf || !len) return -EINVAL;
    uint8_t addr = (uint8_t)(reg & 0x7F);
    struct spi_buf txb[2] = { { .buf = &addr, .len = 1 }, { .buf = NULL, .len = len } };
    struct spi_buf rxb[2] = { { .buf = NULL, .len = 1 }, { .buf = buf,  .len = len } };
    const struct spi_buf_set tx_set = { .buffers = txb, .count = 2 };
    const struct spi_buf_set rx_set = { .buffers = rxb, .count = 2 };
    return spi_transceive(spi_dev, &spi_cfg, &tx_set, &rx_set);
}

/* ---------- Mode control & reset ---------- */
static int rfm69_wait_modeready(int32_t timeout_us)
{
    while (timeout_us > 0) {
        if (rfm69_read_reg(R_REG_IRQFLAGS1) & IRQ1_MODEREADY) return 0;
        k_busy_wait(100);
        timeout_us -= 100;
    }
    return -ETIMEDOUT;
}

static void rfm69_write_opmode(uint8_t mode_bits)
{
    uint8_t v = OPMODE_SEQUENCER_ON | OPMODE_LISTEN_OFF | (mode_bits & 0x1C);
    rfm69_write_reg(R_REG_OPMODE, v);
}

static int rfm69_set_mode_blocking(uint8_t mode_bits)
{
    rfm69_write_opmode(mode_bits);
    if (mode_bits == OPMODE_MODE_SLEEP) { k_msleep(2); return 0; }
    return rfm69_wait_modeready(60000);
}

/* ACTIVE-HIGH reset pulse (>100 us) */
static void rfm69_reset(void)
{
    gpio_pin_set_dt(&reset_gpio, 1);
    k_busy_wait(200);
    gpio_pin_set_dt(&reset_gpio, 0);
    k_msleep(15);
}

/* ---------- Radio config (matches your TX) ---------- */
static int rfm69_init(void)
{
    if (rfm69_set_mode_blocking(OPMODE_MODE_SLEEP) < 0) return -EIO;
    k_msleep(2);
    if (rfm69_set_mode_blocking(OPMODE_MODE_STDBY) < 0) return -EIO;

    /* Packet mode, FSK, no shaping */
    rfm69_write_reg(R_REG_DATAMODUL, 0x00);

    /* Bitrate = 55.556 kbps (32e6 / 0x0240) */
    rfm69_write_reg(R_REG_BITRATEMSB, 0x02);
    rfm69_write_reg(R_REG_BITRATELSB, 0x40);

    /* Fdev ≈ 50 kHz */
    rfm69_write_reg(R_REG_FDEVMSB, 0x03);
    rfm69_write_reg(R_REG_FDEVLSB, 0x33);

    /* 915.000 MHz */
    rfm69_write_reg(R_REG_FRFMSB, 0xE4);
    rfm69_write_reg(R_REG_FRFMID, 0xC0);
    rfm69_write_reg(R_REG_FRFLSB, 0x00);

    /* PA irrelevant in RX; keep benign */
    rfm69_write_reg(R_REG_PALEVEL, 0x80 | 0x00);
    rfm69_write_reg(R_REG_OCP,     0x1A);

    /* Front-end / bandwidth for ~55 kbps */
    rfm69_write_reg(R_REG_LNA,  0x88);
    rfm69_write_reg(R_REG_RXBW, 0x55);

    /* Preamble & sync (must match TX) */
    rfm69_write_reg(R_REG_PREAMBLEMSB, 0x00);
    rfm69_write_reg(R_REG_PREAMBLELSB, 0x03);
    rfm69_write_reg(R_REG_SYNCCONFIG,  0x88); /* SyncOn, size=2 */
    rfm69_write_reg(R_REG_SYNCVALUE1,  0x2D);
    rfm69_write_reg(R_REG_SYNCVALUE2,  0xD4);

    /* Packet: variable length + whitening + CRC */
    rfm69_write_reg(R_REG_PACKETCONFIG1, 0xD0);
    rfm69_write_reg(R_REG_PAYLOADLENGTH, 64);
    rfm69_write_reg(R_REG_FIFOTHRESH,    0x8F);  /* Rx thresh ~15 */
    rfm69_write_reg(R_REG_PACKETCONFIG2, 0x02);  /* AES off */

    /* DIO0→PayloadReady (we poll anyway) */
    rfm69_write_reg(R_REG_DIOMAPPING1, 0x00);

    if (rfm69_set_mode_blocking(OPMODE_MODE_STDBY) < 0) return -EIO;
    return 0;
}

/* Receive one packet (blocking, timeout ms). Returns 0 on success. */
static int rfm69_recv(uint8_t *buf, uint8_t *out_len, int timeout_ms, int *out_rssi_dbm)
{
    if (!buf || !out_len) return -EINVAL;
    *out_len = 0;
    if (out_rssi_dbm) *out_rssi_dbm = 0;

    (void)rfm69_read_reg(R_REG_IRQFLAGS2); /* clear sticky */

    if (rfm69_set_mode_blocking(OPMODE_MODE_RX) < 0) return -EIO;

    int32_t us = timeout_ms * 1000;
    while (us > 0) {
        uint8_t f2 = rfm69_read_reg(R_REG_IRQFLAGS2);
        if (f2 & IRQ2_PAYLOADREADY) break;
        if (f2 & IRQ2_FIFOOVERRUN) LOG_WRN("FIFO overrun");
        k_busy_wait(200);
        us -= 200;
    }

    (void)rfm69_set_mode_blocking(OPMODE_MODE_STDBY);

    if (us <= 0) return -ETIMEDOUT;

    uint8_t len = rfm69_read_reg(R_REG_FIFO); /* first byte = length */
    if (len > 64) len = 64;
    if (rfm69_read_burst(R_REG_FIFO, buf, len) < 0) return -EIO;
    *out_len = len;

    if (out_rssi_dbm) {
        uint8_t rssi_raw = rfm69_read_reg(R_REG_RSSIVALUE);
        *out_rssi_dbm = -((int)rssi_raw / 2);
    }
    return 0;
}

/* ---------- Main ---------- */
int main(void)
{
    LOG_INF("RFM69 RX starting...");

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
        LOG_ERR("Unexpected version; check wiring/power");
        return -1;
    }

    if (rfm69_init() < 0) {
        uint8_t op  = rfm69_read_reg(R_REG_OPMODE);
        uint8_t f1  = rfm69_read_reg(R_REG_IRQFLAGS1);
        uint8_t f2  = rfm69_read_reg(R_REG_IRQFLAGS2);
        LOG_ERR("Init failed. OpMode=0x%02x IRQ1=0x%02x IRQ2=0x%02x", op, f1, f2);
        return -1;
    }
    LOG_INF("RX ready (915 MHz). Listening…");

    while (1) {
        uint8_t payload[64];
        uint8_t len = 0;
        int rssi_dbm = 0;

        int rc = rfm69_recv(payload, &len, 5000, &rssi_dbm);
        if (rc == 0) {
            if (len < sizeof(payload)) payload[len] = '\0';
            LOG_INF("RX (%uB, RSSI~%d dBm): \"%s\"", len, rssi_dbm, (char*)payload);
        } else if (rc == -ETIMEDOUT) {
            LOG_INF("RX timeout (no packet in 5s)");
        } else {
            LOG_ERR("RX error: %d", rc);
        }
    }
}
