#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/lora.h>
#include <zephyr/logging/log.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h> // abs

#include "packet.h"

LOG_MODULE_REGISTER(misogate, LOG_LEVEL_INF);

void main(void)
{
    const struct device *lora_dev;
    int ret;
    static uint32_t rx_ok_count = 0;

    LOG_INF("misogate: init RX");

    lora_dev = DEVICE_DT_GET(DT_ALIAS(lora0));
    if (!device_is_ready(lora_dev)) {
        LOG_ERR("LoRa device not ready");
        return;
    }

    struct lora_modem_config cfg = {
        .frequency      = 915000000UL,
        .bandwidth      = BW_125_KHZ,
        .datarate       = SF_7,
        .coding_rate    = CR_4_5,
        .preamble_len   = 8,
        .tx_power       = 10,
        .tx             = false,
        .iq_inverted    = false,
        .public_network = true,
    };

    ret = lora_config(lora_dev, &cfg);
    if (ret < 0) {
        LOG_ERR("lora_config (RX) failed: %d", ret);
        return;
    }

    LOG_INF("misogate: RX loop start");

    while (1) {
        uint8_t buf[64];
        int16_t rssi;
        int16_t snr;

        int len = lora_recv(lora_dev,
                            buf,
                            sizeof(buf),
                            K_SECONDS(10), // your updated timeout
                            &rssi,
                            &snr);

        if (len > 0) {
            struct sensor_frame f;
            if (packet_parse_secure_frame(buf, len, &f) == 0) {
                rx_ok_count++;

                int16_t t_abs = abs(f.temp_c_times10 % 10);

                LOG_INF("SECURE PKT rx_ok=%u node=%u tx_seq=%u "
                        "X=%u m-uT Y=%u m-uT Z=%u m-uT "
                        "T=%d.%d C RSSI=%d dBm SNR=%d dB len=%d",
                        rx_ok_count,
                        (unsigned)f.node_id,
                        (unsigned)f.tx_seq,
                        (unsigned)f.x_uT_milli,
                        (unsigned)f.y_uT_milli,
                        (unsigned)f.z_uT_milli,
                        f.temp_c_times10 / 10,
                        t_abs,
                        rssi,
                        snr,
                        len);

                // TODO: forward as JSON over UART to Nordic

            } else {
                LOG_WRN("SECURITY DROP len=%d RSSI=%d SNR=%d", len, rssi, snr);
            }
        } else {
            LOG_INF("misogate: waiting...");
        }
    }
}
