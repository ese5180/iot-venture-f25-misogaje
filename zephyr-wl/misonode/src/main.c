#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/lora.h>
#include <zephyr/logging/log.h>
#include <string.h>
#include <stdint.h>

#include "mag.h"
#include "packet.h"

LOG_MODULE_REGISTER(misonode, LOG_LEVEL_INF);

#define NODE_ID 0x01

void main(void)
{
    const struct device *lora_dev;
    int ret;
    static uint32_t tx_seq = 0;

    LOG_INF("misonode: booted, starting LoRa TX loop");

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
        .tx             = true,
        .iq_inverted    = false,
        .public_network = true,
    };

    ret = lora_config(lora_dev, &cfg);
    if (ret < 0) {
        LOG_ERR("lora_config (TX) failed: %d", ret);
        return;
    }

    while (1) {
        struct mag_sample m;
        uint8_t frame[32];
        size_t frame_len;

        mag_read(&m);

        frame_len = packet_build_secure_frame(
            NODE_ID,
            tx_seq,
            &m,
            frame,
            sizeof(frame)
        );

        if (frame_len == 0) {
            LOG_ERR("packet_build_secure_frame failed");
        } else {
            ret = lora_send(lora_dev, frame, frame_len);
            if (ret < 0) {
                LOG_ERR("lora_send err %d", ret);
            } else {
                LOG_INF("misonode: sent node=%u seq=%u len=%u",
                        (unsigned)NODE_ID,
                        (unsigned)tx_seq,
                        (unsigned)frame_len);
                tx_seq++;
            }
        }

        k_sleep(K_SECONDS(5)); // your current 5s spacing
    }
}
