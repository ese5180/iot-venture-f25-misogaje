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

size_t packet_build_secure_frame_encmac(uint8_t node_id, uint32_t tx_seq,
    const struct mag_sample *m_in, uint8_t *out, size_t out_max);

void main(void)
{
    const struct device *lora = DEVICE_DT_GET(DT_ALIAS(lora0));
    if (!device_is_ready(lora)) {
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
    if (lora_config(lora, &cfg) < 0) {
        LOG_ERR("lora_config failed");
        return;
    }

    LOG_INF("misonode: TX (Encrypt-then-MAC, SipHash + stream)");

    uint32_t tx_seq = 0;
    while (1) {
        struct mag_sample m;
        mag_read(&m);

        uint8_t frame[SECURE_FRAME_LEN];
        size_t len = packet_build_secure_frame_encmac(NODE_ID, tx_seq, &m, frame, sizeof(frame));
        if (len == 0) {
            LOG_ERR("build frame failed");
        } else {
            int rc = lora_send(lora, frame, len);
            if (rc < 0) LOG_ERR("lora_send err %d", rc);
            else        LOG_INF("sent node=%u seq=%u len=%u", NODE_ID, tx_seq, (unsigned)len);
            tx_seq++;
        }
        k_sleep(K_SECONDS(5));
    }
}
