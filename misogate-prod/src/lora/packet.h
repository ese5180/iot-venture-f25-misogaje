#pragma once
#include <stddef.h>
#include <stdint.h>

/* Frame & payload layout (unchanged size: 28 bytes) */
#define MSG_TYPE_SENSOR 0x01
#define SENSOR_PLAINTEXT_LEN 15
#define TAG_LEN 8
#define SECURE_FRAME_LEN (1 + 4 + SENSOR_PLAINTEXT_LEN + TAG_LEN)

/* Sensor struct used at the app edges */
struct sensor_frame {
  uint8_t node_id;
  uint32_t tx_seq;
  int32_t x_uT_milli;
  int32_t y_uT_milli;
  int32_t z_uT_milli;
  int16_t temp_c_times10;
};

/* --- helpers to pack/unpack 15B sensor payload --- */
static inline void pack_sensor_payload(uint8_t *buf,
                                       const struct sensor_frame *m) {
  buf[0] = MSG_TYPE_SENSOR;

  uint32_t ux = (uint32_t)m->x_uT_milli;
  uint32_t uy = (uint32_t)m->y_uT_milli;
  uint32_t uz = (uint32_t)m->z_uT_milli;

  buf[1] = (uint8_t)(ux >> 0);
  buf[2] = (uint8_t)(ux >> 8);
  buf[3] = (uint8_t)(ux >> 16);
  buf[4] = (uint8_t)(ux >> 24);

  buf[5] = (uint8_t)(uy >> 0);
  buf[6] = (uint8_t)(uy >> 8);
  buf[7] = (uint8_t)(uy >> 16);
  buf[8] = (uint8_t)(uy >> 24);

  buf[9] = (uint8_t)(uz >> 0);
  buf[10] = (uint8_t)(uz >> 8);
  buf[11] = (uint8_t)(uz >> 16);
  buf[12] = (uint8_t)(uz >> 24);

  uint16_t t = (uint16_t)m->temp_c_times10;
  buf[13] = (uint8_t)(t >> 0);
  buf[14] = (uint8_t)(t >> 8);
}

static inline int unpack_sensor_payload(const uint8_t *p,
                                        struct sensor_frame *out) {
  if (p[0] != MSG_TYPE_SENSOR)
    return -1;

  uint32_t ux = (uint32_t)p[1] | ((uint32_t)p[2] << 8) |
                ((uint32_t)p[3] << 16) | ((uint32_t)p[4] << 24);
  uint32_t uy = (uint32_t)p[5] | ((uint32_t)p[6] << 8) |
                ((uint32_t)p[7] << 16) | ((uint32_t)p[8] << 24);
  uint32_t uz = (uint32_t)p[9] | ((uint32_t)p[10] << 8) |
                ((uint32_t)p[11] << 16) | ((uint32_t)p[12] << 24);

  out->x_uT_milli = (int32_t)ux;
  out->y_uT_milli = (int32_t)uy;
  out->z_uT_milli = (int32_t)uz;

  out->temp_c_times10 = (int16_t)((uint16_t)p[13] | ((uint16_t)p[14] << 8));
  return 0;
}

/**
 * @brief Parse and decrypt a secure LoRa frame using Encrypt-then-MAC
 *
 * @param in Input buffer containing the encrypted frame
 * @param in_len Length of input buffer
 * @param out Pointer to sensor_frame structure to populate
 *
 * @return 0 on success, negative on failure (auth fail, replay, etc.)
 */
int packet_parse_secure_frame_encmac(const uint8_t *in, size_t in_len,
                                     struct sensor_frame *out);
