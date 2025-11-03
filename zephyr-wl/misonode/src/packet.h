#pragma once
#include <stdint.h>
#include <stddef.h>
#include "mag.h"

// Application payload type
#define MSG_TYPE_SENSOR 0x01

// Length of the plaintext sensor payload (msg_type + X/Y/Z/temp)
#define SENSOR_PLAINTEXT_LEN 15

// Length of the final secure LoRa frame (node_id + tx_seq + ciphertext + mic)
#define SECURE_FRAME_LEN (1 + 4 + SENSOR_PLAINTEXT_LEN + 4)

size_t packet_build_secure_frame(
    uint8_t  node_id,
    uint32_t tx_seq,
    const struct mag_sample *m,
    uint8_t *out_buf,
    size_t   out_buf_max
);
