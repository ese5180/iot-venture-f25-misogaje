

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/logging/log.h>
#include <string.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

/* ================= LoRa 参数 ================= */
#define LORA_FREQ_HZ   915000000UL
#define MAX_PAYLOAD    64


static int sx1276_read_reg(uint8_t reg, uint8_t *val);  // ✅ 添加这行
/* ================= SPI 设置 ================= */
static struct spi_dt_spec lora_spi =
    SPI_DT_SPEC_GET(DT_NODELABEL(rfm95),
                    SPI_WORD_SET(8) | SPI_TRANSFER_MSB,
                    0);

/* ================= SPI 基础读写 ================= */
static void sx1276_write_reg(uint8_t reg, uint8_t val)
{

     if (reg == 0x01) {
        val |= 0x80;  // 把 bit7=1
        // LOG_INF("→ LoRa bit forced ON for RegOpMode write");
    }
    uint8_t tx_buf[2] = { reg | 0x80, val };
    struct spi_buf tx = { .buf = tx_buf, .len = 2 };
    const struct spi_buf_set txs = { .buffers = &tx, .count = 1 };
     int ret = spi_write_dt(&lora_spi, &txs);

     if (ret != 0) {
        LOG_ERR("SPI write failed! reg=0x%02X ret=%d", reg, ret);
     }

     // ✅ 验证 0x01 寄存器的写入
    if (reg == 0x01) {
        k_msleep(1);
        uint8_t readback;
        sx1276_read_reg(0x01, &readback);
        
        // if (readback != val) {
        //     LOG_ERR("Reg 0x01 verify FAILED! Wrote:0x%02X Read:0x%02X", val, readback);
        // } else {
        //     LOG_INF("Reg 0x01 verify OK: 0x%02X", readback);
        // }
    }
    
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

/* ================= LoRa 初始化 ================= */
static void sx1276_set_freq(uint32_t freq)
{
    uint64_t frf = ((uint64_t)freq << 19) / 32000000;
    sx1276_write_reg(0x06, (frf >> 16) & 0xFF);
    sx1276_write_reg(0x07, (frf >> 8) & 0xFF);
    sx1276_write_reg(0x08, (frf) & 0xFF);
}

 static void sx1276_reset(void)
{
    const struct device *gpio_rst = DEVICE_DT_GET(DT_NODELABEL(gpio1)); // P1.x
    if (!device_is_ready(gpio_rst)) {
        LOG_ERR("gpio1 not ready!");
        return;
    }

    gpio_pin_configure(gpio_rst, 9, GPIO_OUTPUT_ACTIVE); // P1.09

    gpio_pin_set(gpio_rst, 9, 0);   // 拉低复位
    k_msleep(2);                    // >= 1ms
    gpio_pin_set(gpio_rst, 9, 1);   // 拉高
    k_msleep(10);                   // 给芯片时间稳定
    //LOG_INF("SX1276 RST done");
}



static void sx1276_init_lora(void)
{
    sx1276_reset();
    
    LOG_INF("SX1276 init...");
    sx1276_write_reg(0x01, 0x80);  // Sleep + LoRa
    k_msleep(5);
    sx1276_write_reg(0x01, 0x81 | 0x80);  // Standby

    sx1276_set_freq(LORA_FREQ_HZ);
    sx1276_write_reg(0x1D, 0x72);  // BW125kHz, CR4/5
    sx1276_write_reg(0x1E, 0x74);  // SF7 + CRC ON
    sx1276_write_reg(0x26, 0x04);  // LowDataRateOptimize=OFF, AGC=ON
    sx1276_write_reg(0x09, 0x8F);  // PA_BOOST + 14dBm
    sx1276_write_reg(0x12, 0xFF);  // Clear IRQ

    sx1276_write_reg(0x39, 0x34); // SyncWord
    sx1276_write_reg(0x24, 0x00); // HopPeriod OFF
    sx1276_write_reg(0x0C, 0x23); // LNA Boost
    sx1276_write_reg(0x33, 0x27); // InvertIQ Normal
    sx1276_write_reg(0x3B, 0x1D); // InvertIQ2 固定值

    sx1276_write_reg(0x0E, 0x80); // ✅ TX FIFO base addr
    sx1276_write_reg(0x0F, 0x00); // ✅ RX FIFO base addr




   // LOG_INF("SX1276 basic config done.");
}



static void sx1276_enter_rx(void)
{
    //sx1276_write_reg(0x01, 0x80); // Sleep
  // k_msleep(2);
    sx1276_write_reg(0x01, 0x81 | 0x80); // Standby

    // TX FIFO base = 0x80（发射端区域）
    sx1276_write_reg(0x0E, 0x80);

    // RX FIFO base = 0x00（接收端区域）
    sx1276_write_reg(0x0F, 0x00);

    // FIFO 指针回到 RX 区域头
    sx1276_write_reg(0x0D, 0x00);

    // 清中断 & DIO 映射到 RxDone
    sx1276_write_reg(0x12, 0xFF);
    sx1276_write_reg(0x40, 0x00);

    // 进入连续接收
    sx1276_write_reg(0x01, 0x85 | 0x80);

   // LOG_INF("RX ON");
}


/* ================= FIFO 读取函数 ================= */
static void sx1276_read_fifo(uint8_t *buf, uint8_t len)
{
    uint8_t tx_buf[1 + MAX_PAYLOAD] = { 0x00 };
    uint8_t rx_buf[1 + MAX_PAYLOAD] = { 0 };
    tx_buf[0] = 0x00; // FIFO地址

    struct spi_buf tx = { .buf = tx_buf, .len = len + 1 };
    struct spi_buf rx = { .buf = rx_buf, .len = len + 1 };
    const struct spi_buf_set txs = { .buffers = &tx, .count = 1 };
    const struct spi_buf_set rxs = { .buffers = &rx, .count = 1 };
    spi_transceive_dt(&lora_spi, &txs, &rxs);

    memcpy(buf, &rx_buf[1], len);
}

/* ================= 主程序 ================= */
void main(void)
{
    LOG_INF("==== SX1276 RX Debug Mode ====");

    if (!device_is_ready(lora_spi.bus)) {
        LOG_ERR("SPI bus not ready!");
        return;
    }

    sx1276_init_lora();
    sx1276_enter_rx();

    uint8_t irq, opmode, pktlen, fifo_rx_curr, rssi, modem_stat;
    uint8_t modemstat, hopchan;   // <--- 新增
    uint8_t buf[MAX_PAYLOAD];
  
while (1)
{
    // 1. 读取状态
    sx1276_read_reg(0x12, &irq);
    sx1276_read_reg(0x1C, &hopchan);
    sx1276_read_reg(0x01, &opmode);
     sx1276_read_reg(0x18, &modem_stat); 

   
    if ((opmode & 0x87) != 0x85) {
   // LOG_ERR("!! Mode wrong: %02X, FULL RESET...", opmode);
    
    // ✅ 直接调用初始化函数
    sx1276_init_lora();
    sx1276_enter_rx();
    
    uint8_t verify_modem;
    sx1276_read_reg(0x18, &verify_modem);
    //LOG_WRN("Full reset done, ModemStat=0x%02X", verify_modem);
    
    continue;
   }


    if (irq & 0x40)  // RxDone
    {
        bool CrcError = (irq & 0x20);
        if (!CrcError) {
          uint8_t fifo_rx_curr, pktlen;
          sx1276_read_reg(0x10, &fifo_rx_curr);
          sx1276_read_reg(0x13, &pktlen);
        
          if (pktlen > MAX_PAYLOAD) pktlen = MAX_PAYLOAD;
        
            sx1276_write_reg(0x0D, fifo_rx_curr);
            sx1276_read_fifo(buf, pktlen);
            buf[pktlen] = 0;
            LOG_INF("RECV: %s", buf);
          }  else {
            LOG_WRN("CRC error, dropped");
          }

          sx1276_write_reg(0x12, 0x40);
    
          // 重置FIFO
           sx1276_write_reg(0x0D, 0x00);
    
         
           // LOG_INF("Packet handled, back to RX mode");
         
       }

           k_msleep(50);

 
     }
}








////////




