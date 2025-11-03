#pragma once
#include <stdint.h>
#include <stddef.h>

#define MSG_TYPE_SENSOR 0x01

// same as node; must match
#define SENSOR_PLAINTEXT_LEN 15
#define SECURE_FRAME_LEN     (1 + 4 + SENSOR_PLAINTEXT_LEN + 4)

struct sensor_frame {
    uint8_t  node_id;
    uint32_t tx_seq;
    uint32_t x_uT_milli;
    uint32_t y_uT_milli;
    uint32_t z_uT_milli;
    int16_t  temp_c_times10;
};

// returns 0 if valid+parsed, -1 if MIC fail / replay / bad len / bad type
int packet_parse_secure_frame(const uint8_t *in_buf,
                              size_t in_len,
                              struct sensor_frame *out);
