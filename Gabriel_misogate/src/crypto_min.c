#include "crypto_min.h"
#include "siphash.h"
#include <string.h>

static void sip_to_16(uint8_t out16[16], const uint8_t key[16],
                      const uint8_t *label, size_t lab_len) {
  uint8_t tag[8];
  siphash24(tag, label, lab_len, key);
  memcpy(out16, tag, 8);

  uint8_t lab2[32];
  size_t n = 0;
  for (; n < lab_len && n < sizeof(lab2) - 1; ++n)
    lab2[n] = label[n];
  lab2[n++] = 0xA5; // domain separator
  siphash24(tag, lab2, n, key);
  memcpy(out16 + 8, tag, 8);
}

void kdf_split_keys(const uint8_t k_master[16], uint8_t node_id,
                    uint8_t K_enc_out[16], uint8_t K_mac_out[16]) {
  uint8_t labelE[6] = {'E', 'N', 'C', node_id, 0x00, 0x01};
  uint8_t labelM[6] = {'M', 'A', 'C', node_id, 0x00, 0x01};
  sip_to_16(K_enc_out, k_master, labelE, sizeof(labelE));
  sip_to_16(K_mac_out, k_master, labelM, sizeof(labelM));
}

void keystream_from_seq(uint8_t *out, size_t n, const uint8_t K_enc[16],
                        uint32_t tx_seq) {
  /* Generate 8B blocks: SipHash(K_enc, 'S' || tx_seq || block#) */
  uint8_t in[1 + 4 + 4];
  in[0] = 'S';
  in[1] = (uint8_t)(tx_seq >> 0);
  in[2] = (uint8_t)(tx_seq >> 8);
  in[3] = (uint8_t)(tx_seq >> 16);
  in[4] = (uint8_t)(tx_seq >> 24);

  uint8_t tag[8];
  uint32_t block = 0;
  size_t produced = 0;
  while (produced < n) {
    in[5] = (uint8_t)(block >> 0);
    in[6] = (uint8_t)(block >> 8);
    in[7] = (uint8_t)(block >> 16);
    in[8] = (uint8_t)(block >> 24);
    siphash24(tag, in, sizeof(in), K_enc);
    size_t take = (n - produced < 8) ? (n - produced) : 8;
    for (size_t i = 0; i < take; ++i)
      out[produced + i] = tag[i];
    produced += take;
    block++;
  }
}
