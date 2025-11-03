#include <zephyr/sys/byteorder.h>
#include <stdint.h>
#include <stddef.h>
#include "packet.h"

// replay tracking (simple global for now)
static uint32_t last_seq_seen[256]; // zero-init, fine for demo

// === must match the node's fake_mic_calc() ===
static uint32_t fake_mic_calc(uint8_t node_id,
                              uint32_t tx_seq,
                              const uint8_t *plaintext,
                              size_t plaintext_len)
{
    uint32_t mic = 0;
    mic ^= node_id;
    mic ^= tx_seq;
    for (size_t i = 0; i < plaintext_len; i++) {
        mic ^= plaintext[i];
        mic = (mic << 1) | (mic >> 31);
    }
    return mic;
}

int packet_parse_secure_frame(const uint8_t *in_buf,
                              size_t in_len,
                              struct sensor_frame *out)
{
    if (in_len < SECURE_FRAME_LEN) {
        return -1;
    }

    // 1. peel outer header
    uint8_t  node_id = in_buf[0];
    uint32_t tx_seq  = sys_get_le32(&in_buf[1]);

    // ciphertext pointer (currently plaintext in disguise)
    const uint8_t *ciphertext = &in_buf[5];

    // MIC at the end
    uint32_t mic_rx = sys_get_le32(&in_buf[5 + SENSOR_PLAINTEXT_LEN]);

    // 2. "decrypt"
    // right now we just treat ciphertext as plaintext
    uint8_t plaintext[SENSOR_PLAINTEXT_LEN];
    for (size_t i = 0; i < SENSOR_PLAINTEXT_LEN; i++) {
        plaintext[i] = ciphertext[i];
    }

    // 3. verify MIC
    uint32_t mic_exp = fake_mic_calc(node_id, tx_seq,
                                     plaintext,
                                     SENSOR_PLAINTEXT_LEN);
    if (mic_rx != mic_exp) {
        // integrity/auth check failed
        return -1;
    }

    // 4. replay protection
    // drop if seq is not strictly increasing
    if (tx_seq <= last_seq_seen[node_id]) {
        return -1;
    }
    last_seq_seen[node_id] = tx_seq;

    // 5. parse plaintext sensor payload
    if (plaintext[0] != MSG_TYPE_SENSOR) {
        return -1;
    }

    out->node_id         = node_id;
    out->tx_seq          = tx_seq;
    out->x_uT_milli      = sys_get_le32(&plaintext[1]);
    out->y_uT_milli      = sys_get_le32(&plaintext[5]);
    out->z_uT_milli      = sys_get_le32(&plaintext[9]);
    out->temp_c_times10  = (int16_t)sys_get_le16(&plaintext[13]);

    return 0;
}
