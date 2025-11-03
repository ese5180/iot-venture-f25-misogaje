#include "packet.h"

// little-endian helpers
static inline void put_le32(uint8_t *buf, uint32_t val)
{
    buf[0] = (uint8_t)((val >> 0)  & 0xFF);
    buf[1] = (uint8_t)((val >> 8)  & 0xFF);
    buf[2] = (uint8_t)((val >> 16) & 0xFF);
    buf[3] = (uint8_t)((val >> 24) & 0xFF);
}

static inline void put_le16(uint8_t *buf, uint16_t val)
{
    buf[0] = (uint8_t)((val >> 0) & 0xFF);
    buf[1] = (uint8_t)((val >> 8) & 0xFF);
}

// super placeholder "MIC":
// for now just XOR all bytes of (node_id, tx_seq, plaintext) into a uint32_t
// NOTE: THIS IS NOT CRYPTO. It's a stub to prove plumbing.
// Later this becomes AES-CCM auth tag.
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
        mic = (mic << 1) | (mic >> 31); // rotate a bit so order matters
    }
    return mic;
}

size_t packet_build_secure_frame(
    uint8_t  node_id,
    uint32_t tx_seq,
    const struct mag_sample *m,
    uint8_t *out_buf,
    size_t   out_buf_max
)
{
    if (out_buf_max < SECURE_FRAME_LEN) {
        return 0;
    }

    // 1. Build plaintext sensor payload (15 bytes)
    // Layout:
    // [0]    msg_type
    // [1..4] x_uT_milli
    // [5..8] y_uT_milli
    // [9..12] z_uT_milli
    // [13..14] temp_c_times10
    uint8_t plaintext[SENSOR_PLAINTEXT_LEN];

    plaintext[0] = MSG_TYPE_SENSOR;
    put_le32(&plaintext[1],  m->x_uT_milli);
    put_le32(&plaintext[5],  m->y_uT_milli);
    put_le32(&plaintext[9],  m->z_uT_milli);
    put_le16(&plaintext[13], (uint16_t)m->temp_c_times10);

    // 2. "Encrypt" -> ciphertext
    // For now: no actual encryption, copy plaintext.
    // Later: AES-CCM or AES-CTR+tag using (node_id, tx_seq) as nonce.
    uint8_t ciphertext[SENSOR_PLAINTEXT_LEN];
    for (size_t i = 0; i < SENSOR_PLAINTEXT_LEN; i++) {
        ciphertext[i] = plaintext[i];
    }

    // 3. Compute MIC over node_id + tx_seq + plaintext
    // Later: this becomes the CCM authentication tag.
    uint32_t mic_val = fake_mic_calc(node_id, tx_seq,
                                     plaintext,
                                     SENSOR_PLAINTEXT_LEN);

    // 4. Assemble secure frame into out_buf:
    // [0]      node_id             (1 byte)
    // [1..4]   tx_seq              (4 bytes LE)
    // [5..19]  ciphertext (15 bytes)
    // [20..23] mic (4 bytes LE)

    out_buf[0] = node_id;
    put_le32(&out_buf[1], tx_seq);

    for (size_t i = 0; i < SENSOR_PLAINTEXT_LEN; i++) {
        out_buf[5 + i] = ciphertext[i];
    }

    put_le32(&out_buf[5 + SENSOR_PLAINTEXT_LEN], mic_val);

    return SECURE_FRAME_LEN; // = 24
}
