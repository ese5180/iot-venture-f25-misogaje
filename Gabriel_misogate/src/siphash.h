#pragma once
#include <stddef.h>
#include <stdint.h>

/* SipHash-2-4, 64-bit tag */
void siphash24(uint8_t tag[8], const uint8_t *msg, size_t msg_len,
               const uint8_t key[16]);
