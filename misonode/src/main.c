// misonode_rfm69_tx.c
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
#define RFM69_REG_VERSION       0x10

/* OpMode bits */
#define OPMODE_SEQUENCER_ON     0x80
#define OPMODE_LISTEN_OFF       0x00
#define OPMODE_MODE_SLEEP       0x00
#define OPMODE_MODE_STDBY       0x04
#define OPMODE_MODE_FS          0x08
#define OPMODE_MODE_TX          0x0C
#define OPMODE_MODE_RX          0x10

/* IRQ bits */
#define IRQ1_MODEREADY          BIT(7)
#define IRQ2_PACKETSENT         BIT(3)
#define IRQ2_FIFOOVERRUN        BIT(4)

/* ---------- Board wiring (nRF7002 DK) ----------
 * SPI bus: Arduino header SPI => DT node arduino_spi
 * CS:     D10 = P1.12
 * RESET:  D9  = P1.10  (ACTIVE HIGH)
 * DIO0:   optional (unused here)
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
    .frequency = 1000000, /* start at 1 MHz; raise later if desired */
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

/* burst write (for FIFO) */
static int rfm69_write_burst(uint8_t reg, const uint8_t *data, size_t len)
{
    uint8_t addr = reg | 0x80;
    struct spi_buf bufs[2] = {
        { .buf = &addr,       .len = 1 },
        { .buf = (void*)data, .len = len },
    };
    const struct spi_buf_set tx_set = { .buffers = bufs, .count = 2 };
    int ret = spi_write(spi_dev, &spi_cfg, &tx_set);
    if (ret < 0) LOG_ERR("SPI burst write failed: %d", ret);
    return ret;
}

/* ---------- Mode control & reset ---------- */
static void rfm69_write_opmode(uint8_t mode_bits)
{
    /* Sequencer ON, Listen OFF, set Mode[4:2] explicitly */
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

/* IMPORTANT: ModeReady never asserts in SLEEP */
static int rfm69_set_mode_blocking(uint8_t mode_bits)
{
    rfm69_write_opmode(mode_bits);
    if (mode_bits == OPMODE_MODE_SLEEP) {
        k_msleep(2);
        return 0;
    }
    return rfm69_wait_modeready(50000); /* up to 50 ms */
}

/* ACTIVE-HIGH reset pulse (>100 us), then wait */
static void rfm69_reset(void)
{
    gpio_pin_set_dt(&reset_gpio, 1);
    k_busy_wait(200);
    gpio_pin_set_dt(&reset_gpio, 0);
    k_msleep(15);
}

/* Sleep (no wait) -> Standby (wait for ModeReady) */
static int rfm69_wake(void)
{
    if (rfm69_set_mode_blocking(OPMODE_MODE_SLEEP) < 0) return -EIO;
    k_msleep(2);
    if (rfm69_set_mode_blocking(OPMODE_MODE_STDBY) < 0) return -EIO;
    return 0;
}

/* ---------- Radio config & TX ---------- */
static int rfm69_init(void)
{
    if (rfm69_wake() < 0) {
        LOG_ERR("Wake sequence failed");
        return -EIO;
    }

    /* Packet mode, FSK, no shaping */
    rfm69_write_reg(R_REG_DATAMODUL, 0x00);

    /* Bitrate = 55.556 kbps  (32e6 / 0x0240) */
    rfm69_write_reg(R_REG_BITRATEMSB, 0x02);
    rfm69_write_reg(R_REG_BITRATELSB, 0x40);

    /* Fdev ≈ 50 kHz */
    rfm69_write_reg(R_REG_FDEVMSB, 0x03);
    rfm69_write_reg(R_REG_FDEVLSB, 0x33);

    /* -------- Frequency --------
     * 915.000 MHz -> 0xE4 0xC0 0x00
     * 868.000 MHz -> 0xD9 0x00 0x00  (use these three for EU868)
     */
    rfm69_write_reg(R_REG_FRFMSB, 0xE4);
    rfm69_write_reg(R_REG_FRFMID, 0xC0);
    rfm69_write_reg(R_REG_FRFLSB, 0x00);

    /* PA: PA0 (~13 dBm). For HCW boost modes, configure TESTPAx later if needed. */
    rfm69_write_reg(R_REG_PALEVEL, 0x80 | 0x1F);  // Pa0On | 0x1F
    rfm69_write_reg(R_REG_OCP,     0x1A);         // OCP on (95 mA)

    /* RX params (not critical for TX-only) */
    rfm69_write_reg(R_REG_LNA,  0x88);  // 200 ohm, auto gain
    rfm69_write_reg(R_REG_RXBW, 0x55);  // ~83 kHz BW, OK for 55 kbps

    /* Preamble 3 bytes */
    rfm69_write_reg(R_REG_PREAMBLEMSB, 0x00);
    rfm69_write_reg(R_REG_PREAMBLELSB, 0x03);

    /* Sync: on, 2 bytes: 0x2D, 0xD4 */
    rfm69_write_reg(R_REG_SYNCCONFIG, 0x88);
    rfm69_write_reg(R_REG_SYNCVALUE1, 0x2D);
    rfm69_write_reg(R_REG_SYNCVALUE2, 0xD4);

    /* Packet: variable length + whitening + CRC */
    rfm69_write_reg(R_REG_PACKETCONFIG1, 0xD0);
    rfm69_write_reg(R_REG_PAYLOADLENGTH, 64);        // cap for var-len
    rfm69_write_reg(R_REG_FIFOTHRESH,   0x80 | 15);  // TxStart on FifoNotEmpty
    rfm69_write_reg(R_REG_PACKETCONFIG2,0x02);       // AES off (for now)

    /* Map DIO0 to PacketSent in TX (00), though we poll anyway */
    rfm69_write_reg(R_REG_DIOMAPPING1, 0x00);

    /* Back to Standby and confirm ModeReady */
    if (rfm69_set_mode_blocking(OPMODE_MODE_STDBY) < 0) {
        LOG_ERR("Standby ModeReady timeout after config");
        return -EIO;
    }
    return 0;
}

/* Send one packet (len <= 64). Variable-length => first byte is len. */
static int rfm69_send(const uint8_t *data, uint8_t len)
{
    if (!data || len == 0 || len > 64) return -EINVAL;

    /* Clear stale flags by reading */
    (void)rfm69_read_reg(R_REG_IRQFLAGS2);

    uint8_t frame[65];
    frame[0] = len;
    memcpy(&frame[1], data, len);

    /* Fill FIFO */
    int ret = rfm69_write_burst(R_REG_FIFO, frame, len + 1);
    if (ret < 0) return ret;

    /* Enter TX and wait for PacketSent */
    if (rfm69_set_mode_blocking(OPMODE_MODE_TX) < 0) return -EIO;

    int32_t timeout_us = 600000; /* generous for short payloads */
    while (timeout_us > 0) {
        uint8_t f2 = rfm69_read_reg(R_REG_IRQFLAGS2);
        if (f2 & IRQ2_PACKETSENT) break;
        if (f2 & IRQ2_FIFOOVERRUN) LOG_WRN("FIFO overrun during TX");
        k_busy_wait(100);
        timeout_us -= 100;
    }

    /* Back to standby */
    (void)rfm69_set_mode_blocking(OPMODE_MODE_STDBY);

    if (timeout_us <= 0) {
        LOG_ERR("PacketSent timeout");
        return -ETIMEDOUT;
    }
    return 0;
}

/* ---------- Main ---------- */
int main(void)
{
    LOG_INF("Misonode starting...");

    if (!device_is_ready(spi_dev)) {
        LOG_ERR("SPI device not ready");
        return -1;
    }
    LOG_INF("SPI device ready");

    if (!device_is_ready(cs_gpio.port) || !device_is_ready(reset_gpio.port)) {
        LOG_ERR("GPIO ports not ready");
        return -1;
    }
    if (gpio_pin_configure_dt(&reset_gpio, GPIO_OUTPUT_INACTIVE) < 0) {
        LOG_ERR("Reset GPIO configure failed");
        return -1;
    }

    /* Reset */
    rfm69_reset();

    /* Probe version */
    uint8_t ver = rfm69_read_reg(RFM69_REG_VERSION);
    LOG_INF("RFM69 Version: 0x%02x (expect 0x24)", ver);
    if (ver != 0x24) {
        LOG_ERR("Unexpected version; check wiring/power");
        return -1;
    } else {
        LOG_INF("RFM69HCW detected");
    }

    /* Radio init */
    if (rfm69_init() < 0) {
        uint8_t op  = rfm69_read_reg(R_REG_OPMODE);
        uint8_t f1  = rfm69_read_reg(R_REG_IRQFLAGS1);
        uint8_t f2  = rfm69_read_reg(R_REG_IRQFLAGS2);
        LOG_ERR("RFM69 init failed. OpMode=0x%02x IRQ1=0x%02x IRQ2=0x%02x", op, f1, f2);
        return -1;
    }
    LOG_INF("RFM69 init OK");

    /* TX loop */
    uint32_t counter = 0;
    while (1) {
        uint8_t payload[48];
        int n = snprintk((char*)payload, sizeof(payload),
                         "nRF7002DK->RFM69 test #%u", counter++);
        if (n < 0) n = 0;

        LOG_INF("TX: \"%s\" (%d bytes)", payload, n);
        int ret = rfm69_send(payload, (uint8_t)n);
        if (ret == 0) {
            LOG_INF("Packet sent");
        } else {
            LOG_ERR("Send failed: %d", ret);
        }

        k_sleep(K_SECONDS(2));
    }
}
