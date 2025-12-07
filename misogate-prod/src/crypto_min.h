#pragma once
#include <stdint.h>
#include <stddef.h>

/* Derive ENC and MAC subkeys from a 16-byte per-node master key */
void kdf_split_keys(const uint8_t k_master[16], uint8_t node_id,
                    uint8_t K_enc_out[16], uint8_t K_mac_out[16]);

/* Build keystream from K_enc and tx_seq (nonce) */
void keystream_from_seq(uint8_t *out, size_t n,
                        const uint8_t K_enc[16], uint32_t tx_seq);
