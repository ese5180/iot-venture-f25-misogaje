

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/logging/log.h>
#include <string.h>
#include <zephyr/drivers/gpio.h>




#define LORA_NODE DT_ALIAS(lora0)


LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

/*================== LoRa 基本参数 ==================*/
#define LORA_FREQ_HZ  915000000UL  // 915 MHz
#define TX_POWER      14           // dBm
#define MESSAGE       "Hello from nRF7002DK SPI TX"

/*================== SPI 设置 ==================*/
static struct spi_dt_spec lora_spi =
    SPI_DT_SPEC_GET(DT_NODELABEL(rfm95),
                    SPI_WORD_SET(8) | SPI_TRANSFER_MSB,
                    0);

/*================== SPI 写寄存器 ==================*/
static void sx1276_write_reg(uint8_t reg, uint8_t val)
{
    uint8_t tx_buf[2] = { reg | 0x80, val };
    struct spi_buf tx = { .buf = tx_buf, .len = 2 };
    const struct spi_buf_set txs = { .buffers = &tx, .count = 1 };
    spi_write_dt(&lora_spi, &txs);
}

/*================== SPI 读寄存器 ==================*/
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

/*================== 写 FIFO 数据 ==================*/
static void sx1276_write_fifo(const uint8_t *data, uint8_t len)
{
    uint8_t buf[1 + 64];
    buf[0] = 0x80;  // FIFO 寄存器地址 + 写位
    memcpy(&buf[1], data, len);

    struct spi_buf tx = { .buf = buf, .len = len + 1 };
    const struct spi_buf_set txs = { .buffers = &tx, .count = 1 };
    spi_write_dt(&lora_spi, &txs);
}

/*================== 设置频率 ==================*/
static void sx1276_set_freq(uint32_t freq)
{
    uint64_t frf = ((uint64_t)freq << 19) / 32000000;  // Fstep = 32 MHz / 2^19
    sx1276_write_reg(0x06, (frf >> 16) & 0xFF);
    sx1276_write_reg(0x07, (frf >> 8) & 0xFF);
    sx1276_write_reg(0x08, (frf) & 0xFF);
}



static void sx1276_init_lora(void)
{
    sx1276_write_reg(0x01, 0x80); // Sleep + LoRa
    k_msleep(5);
    sx1276_write_reg(0x01, 0x81 | 0x80); // Standby

    sx1276_set_freq(LORA_FREQ_HZ);

    sx1276_write_reg(0x1D, 0x72); // BW 125kHz, CR 4/5
    sx1276_write_reg(0x1E, 0x74); // SF7 + CRC ON
    sx1276_write_reg(0x26, 0x04); // AGC ON

    sx1276_write_reg(0x39, 0x34); // SyncWord 0x34 (必须 TX RX 相同)
    sx1276_write_reg(0x24, 0x00); // HopPeriod OFF
    sx1276_write_reg(0x0C, 0x23); // LNA Boost
    sx1276_write_reg(0x33, 0x27); // Normal IQ
    sx1276_write_reg(0x3B, 0x1D); // IQ2 固定值

    sx1276_write_reg(0x09, 0x8F); // PA_BOOST +14dBm
    sx1276_write_reg(0x12, 0xFF); // Clear IRQ
}



// static void sx1276_send_packet(const uint8_t *data, uint8_t len)
// {
//     uint8_t irq = 0, mode = 0,  ptr = 0;

//     LOG_INF("----- TX START -----");

//     /* Check current mode */
//     sx1276_read_reg(0x01, &mode);
//     LOG_INF("Before TX: RegOpMode = 0x%02X", mode);


//     /* 彻底清状态：清中断 + Sleep + Standby */
//     sx1276_write_reg(0x12, 0xFF);   // 清中断
//    // sx1276_write_reg(0x01, 0x80);   // Sleep + LoRa
//     //k_busy_wait(500);
//     sx1276_write_reg(0x01, 0x81 | 0x80);   // Standby + LoRa

    
    
//     /* ✅ 必须重新写回 CRC 设定（因为 Sleep 会清掉） */
//     sx1276_write_reg(0x1E, 0x74); 


//     sx1276_write_reg(0x1D, 0x72); // ✅ BW=125kHz, CR=4/5
//     sx1276_write_reg(0x26, 0x04); // ✅ AGC ON
//     sx1276_write_reg(0x39, 0x34);   // ✅ 补上 SyncWord 恢复



//     /* 重新配置 TX 参数 */
//     //sx1276_write_reg(0x0E, 0x00);

//    // sx1276_write_reg(0x0D, 0x00);

//     // ✅ 正确的 TX FIFO 配置
//     sx1276_write_reg(0x0E, 0x80);  // TX FIFO base address = 0x80
//     sx1276_write_reg(0x0D, 0x80);  // FIFO pointer = 0x80 (TX base)

//     /* Confirm FIFO pointer */
//     sx1276_read_reg(0x0D, &ptr);
//     LOG_INF("FIFO Pointer after write = 0x%02X (expected 0x80)", ptr);

//     sx1276_write_fifo(data, len);
//     sx1276_write_reg(0x22, len);
//     sx1276_write_reg(0x09, 0x8F);   // PA_BOOST + 14dBm

    
//     // 确保 DIO0 = TX_DONE
//     sx1276_write_reg(0x40, 0x00);  // RegDioMapping1 = 0x00 → DIO0 映射到 TX_DONE

    
//     /* 切换 TX 模式 */
//     sx1276_write_reg(0x01, 0x83);
//     LOG_INF(" switch to TX (0x83)...");

//     /* 等待 TX_DONE */
//     int timeout = 3000;
//     while (timeout--) {
//         sx1276_read_reg(0x12, &irq);
//         if (irq & 0x08) break;
//         k_busy_wait(1000);
//     }

//     /* 读取模式和标志 */
//     sx1276_read_reg(0x01, &mode);
//     LOG_INF("After TX: RegOpMode = 0x%02X, RegIrqFlags = 0x%02X", mode, irq);

//     if (irq & 0x08) {
//         LOG_INF("TX_DONE detected!");
//     } else {
//         LOG_WRN("TX_DONE missing");
//     }

//     /* 清中断并回 Standby */
//     sx1276_write_reg(0x12, 0xFF);
//     sx1276_write_reg(0x01, 0x81 | 0x80); // LoRa Standby

// }

static void sx1276_send_packet(const uint8_t *data, uint8_t len)
{
    if (len == 0) return;
    if (len > 64) len = 64;  // SX1276 FIFO 单次最多 64 字节

    uint8_t irq = 0, mode = 0, ptr = 0;

    LOG_INF("----- TX START -----");

    /* 1) 进入 LoRa Standby，清所有中断标志，确保是干净起点 */
    sx1276_write_reg(0x01, 0x81);      // LoRa + Standby
    k_msleep(2);
    sx1276_write_reg(0x12, 0xFF);      // 清所有 IRQ（含 TxDone）

    /* 2) 确保关键参数一致（某些模式切换可能会被芯片重置） */
    sx1276_set_freq(LORA_FREQ_HZ);
    sx1276_write_reg(0x1D, 0x72);      // BW=125kHz, CR=4/5
    sx1276_write_reg(0x1E, 0x74);      // SF7 + CRC ON
    sx1276_write_reg(0x26, 0x04);      // AGC ON
    sx1276_write_reg(0x39, 0x34);      // SyncWord 0x34
    sx1276_write_reg(0x09, 0x8F);      // PA_BOOST +14dBm（必要时改功率）

    /* 3) 正确设置 DIO0=TxDone（如果以后要用中断就不踩坑） */
    sx1276_write_reg(0x40, 0x40);      // DIO0 = 01b = TxDone（注意：0x00 是 RxDone）

    /* 4) 配置 TX FIFO 并写入数据 */
    sx1276_write_reg(0x0E, 0x80);      // TX FIFO base = 0x80
    sx1276_write_reg(0x0D, 0x80);      // FIFO pointer = 0x80
    sx1276_read_reg(0x0D, &ptr);
    LOG_INF("FIFO pointer = 0x%02X (expect 0x80)", ptr);

    sx1276_write_fifo(data, len);
    sx1276_write_reg(0x22, len);       // RegPayloadLength

    /* 5) 进入 TX 模式并等待 TxDone */
    sx1276_write_reg(0x01, 0x83);      // LoRa + TX
    LOG_INF("Switch to TX (0x83)...");

    /* 轮询等待 TxDone；超时做保护（理论上 SF7/125k/29B < 100ms） */
    int i;
    for (i = 0; i < 1000; i++) {       // ~1000 ms 上限
        sx1276_read_reg(0x12, &irq);
        if (irq & 0x08) break;         // TxDone
        k_busy_wait(1000);             // 1 ms
    }

    sx1276_read_reg(0x01, &mode);
    LOG_INF("After TX: RegOpMode=0x%02X, RegIrqFlags=0x%02X", mode, irq);

    if (irq & 0x08) {
        LOG_INF("TX_DONE detected");
    } else {
        LOG_WRN("TX_DONE timeout");
    }

    /* 6) 清 TxDone，并留在 Standby（或按需进入 Sleep） */
    sx1276_write_reg(0x12, 0x08);      // 只清 TxDone 位
    sx1276_write_reg(0x01, 0x81);      // LoRa + Standby
    k_msleep(2);

    /* 如果你想最大省电，也可以改成：
       sx1276_write_reg(0x01, 0x80);   // LoRa + Sleep
    */
}



/*================== 主函数 ==================*/
void main(void)
{
    #if DT_NODE_HAS_PROP(LORA_NODE, dio_gpios)
     LOG_INF("DIO GPIOs found in devicetree (runtime confirm)");
    #else
     LOG_WRN("DIO GPIOs missing in devicetree (runtime confirm)");
    #endif


    LOG_INF("==== SX1276 SPI LoRa TX Test ====");

    sx1276_init_lora();

    static uint32_t packet_counter = 0;  // <--- new counter
    char message[64];                    // buffer for formatted text


    while (1) {
        snprintk(message, sizeof(message), 
                 "Hello #%lu from nRF7002DK SPI TX", 
                 (unsigned long)packet_counter);

          LOG_INF("Sending packet #%lu: %s", 
                 (unsigned long)packet_counter, message);

        
         sx1276_send_packet((const uint8_t *)message, strlen(message));

          // === 3️⃣ 自增编号 ===
        packet_counter++;
        

        k_sleep(K_SECONDS(5));
    }
}

