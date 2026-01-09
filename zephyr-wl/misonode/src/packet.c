// misonode/src/packet.c
#include "packet.h"
#include "crypto_min.h"
#include "mag.h"
#include "siphash.h"
#include <string.h>

/* Per-node 128-bit master key (hardcode for class; store per-node) */
static const uint8_t NODE_MASTER_KEY[16] = {0x4d, 0x69, 0x73, 0x6f, 0x4b, 0x65,
                                            0x79, 0x21, 0x10, 0x22, 0x33, 0x44,
                                            0x55, 0x66, 0x77, 0x88};

size_t packet_build_secure_frame_encmac(uint8_t node_id, uint32_t tx_seq,
                                        const struct mag_sample *m_in,
                                        uint8_t *out, size_t out_max) {
  if (out_max < SECURE_FRAME_LEN)
    return 0;

  // Header
  out[0] = node_id;
  out[1] = (uint8_t)(tx_seq >> 0);
  out[2] = (uint8_t)(tx_seq >> 8);
  out[3] = (uint8_t)(tx_seq >> 16);
  out[4] = (uint8_t)(tx_seq >> 24);

  // Build payload from sample
  struct sensor_frame s = {.node_id = node_id,
                           .tx_seq = tx_seq,
                           .x_uT_milli = m_in->x_uT_milli,
                           .y_uT_milli = m_in->y_uT_milli,
                           .z_uT_milli = m_in->z_uT_milli,
                           .temp_c_times10 = m_in->temp_c_times10};
  uint8_t pt[SENSOR_PLAINTEXT_LEN];
  pack_sensor_payload(pt, &s);

  // Derive subkeys; encrypt (XOR keystream); MAC over AAD||ciphertext
  uint8_t K_enc[16], K_mac[16], ks[SENSOR_PLAINTEXT_LEN];
  kdf_split_keys(NODE_MASTER_KEY, node_id, K_enc, K_mac);
  keystream_from_seq(ks, sizeof(ks), K_enc, tx_seq);

  uint8_t *ct = &out[5];
  for (size_t i = 0; i < SENSOR_PLAINTEXT_LEN; ++i)
    ct[i] = pt[i] ^ ks[i];

  uint8_t mac_input[1 + 4 + SENSOR_PLAINTEXT_LEN];
  mac_input[0] = node_id;
  mac_input[1] = out[1];
  mac_input[2] = out[2];
  mac_input[3] = out[3];
  mac_input[4] = out[4];
  memcpy(&mac_input[5], ct, SENSOR_PLAINTEXT_LEN);

  uint8_t tag[TAG_LEN];
  siphash24(tag, mac_input, sizeof(mac_input), K_mac);

  memcpy(&out[5 + SENSOR_PLAINTEXT_LEN], tag, TAG_LEN);
  return SECURE_FRAME_LEN;
}
