#include <string.h>
#include <stdlib.h>
#include "packet.h"
#include "crypto_min.h"
#include "siphash.h"

/* Same per-node master key as node */
static const uint8_t NODE_MASTER_KEY[16] = {
    0x4d,0x69,0x73,0x6f,0x4b,0x65,0x79,0x21, 0x10,0x22,0x33,0x44,0x55,0x66,0x77,0x88
};

static uint32_t last_seq_seen[256];

int packet_parse_secure_frame_encmac(const uint8_t *in, size_t in_len, struct sensor_frame *out)
{
    if (in_len < SECURE_FRAME_LEN) return -1;

    uint8_t  node_id = in[0];
    uint32_t tx_seq  = (uint32_t)in[1] | ((uint32_t)in[2]<<8)
                     | ((uint32_t)in[3]<<16) | ((uint32_t)in[4]<<24);

    const uint8_t *ct  = &in[5];
    const uint8_t *tag = &in[5 + SENSOR_PLAINTEXT_LEN];

    uint8_t K_enc[16], K_mac[16];
    kdf_split_keys(NODE_MASTER_KEY, node_id, K_enc, K_mac);

    // MAC check first (Encrypt-then-MAC)
    uint8_t mac_input[1 + 4 + SENSOR_PLAINTEXT_LEN];
    mac_input[0] = node_id;
    mac_input[1] = in[1]; mac_input[2] = in[2];
    mac_input[3] = in[3]; mac_input[4] = in[4];
    memcpy(&mac_input[5], ct, SENSOR_PLAINTEXT_LEN);

    uint8_t calc[TAG_LEN];
    siphash24(calc, mac_input, sizeof(mac_input), K_mac);
    if (memcmp(calc, tag, TAG_LEN) != 0) return -1;

    // Replay protection per node
    if (tx_seq <= last_seq_seen[node_id]) return -1;
    last_seq_seen[node_id] = tx_seq;

    // Decrypt

    uint8_t ks[SENSOR_PLAINTEXT_LEN], pt[SENSOR_PLAINTEXT_LEN];
    keystream_from_seq(ks, sizeof(ks), K_enc, tx_seq);
    for (size_t i = 0; i < SENSOR_PLAINTEXT_LEN; ++i) pt[i] = ct[i] ^ ks[i];

    out->node_id = node_id;
    out->tx_seq  = tx_seq;
    return unpack_sensor_payload(pt, out);
}

