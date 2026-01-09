#include "siphash.h"

static inline uint64_t U8TO64_LE(const uint8_t *p) {
  return ((uint64_t)p[0]) | ((uint64_t)p[1] << 8) | ((uint64_t)p[2] << 16) |
         ((uint64_t)p[3] << 24) | ((uint64_t)p[4] << 32) |
         ((uint64_t)p[5] << 40) | ((uint64_t)p[6] << 48) |
         ((uint64_t)p[7] << 56);
}
static inline uint64_t ROTL64(uint64_t x, int b) {
  return (x << b) | (x >> (64 - b));
}
static inline void sipround(uint64_t *v0, uint64_t *v1, uint64_t *v2,
                            uint64_t *v3) {
  *v0 += *v1;
  *v1 = ROTL64(*v1, 13) ^ *v0;
  *v0 = ROTL64(*v0, 32);
  *v2 += *v3;
  *v3 = ROTL64(*v3, 16) ^ *v2;
  *v0 += *v3;
  *v3 = ROTL64(*v3, 21) ^ *v0;
  *v2 += *v1;
  *v1 = ROTL64(*v1, 17) ^ *v2;
  *v2 = ROTL64(*v2, 32);
}

void siphash24(uint8_t out[8], const uint8_t *m, size_t len,
               const uint8_t key[16]) {
  uint64_t k0 = U8TO64_LE(key + 0);
  uint64_t k1 = U8TO64_LE(key + 8);
  uint64_t v0 = 0x736f6d6570736575ULL ^ k0;
  uint64_t v1 = 0x646f72616e646f6dULL ^ k1;
  uint64_t v2 = 0x6c7967656e657261ULL ^ k0;
  uint64_t v3 = 0x7465646279746573ULL ^ k1;

  const uint8_t *end = m + (len & ~((size_t)7));
  int left = (int)(len & 7);

  for (const uint8_t *p = m; p != end; p += 8) {
    uint64_t mi = U8TO64_LE(p);
    v3 ^= mi;
    sipround(&v0, &v1, &v2, &v3);
    sipround(&v0, &v1, &v2, &v3);
    v0 ^= mi;
  }

  uint64_t b = ((uint64_t)len) << 56;
  switch (left) { /* fallthroughs intentional */
  case 7:
    b |= ((uint64_t)m[len - 7]) << 48;
  case 6:
    b |= ((uint64_t)m[len - 6]) << 40;
  case 5:
    b |= ((uint64_t)m[len - 5]) << 32;
  case 4:
    b |= ((uint64_t)m[len - 4]) << 24;
  case 3:
    b |= ((uint64_t)m[len - 3]) << 16;
  case 2:
    b |= ((uint64_t)m[len - 2]) << 8;
  case 1:
    b |= ((uint64_t)m[len - 1]);
  default:
    break;
  }

  v3 ^= b;
  sipround(&v0, &v1, &v2, &v3);
  sipround(&v0, &v1, &v2, &v3);
  v0 ^= b;
  v2 ^= 0xff;
  sipround(&v0, &v1, &v2, &v3);
  sipround(&v0, &v1, &v2, &v3);
  sipround(&v0, &v1, &v2, &v3);
  sipround(&v0, &v1, &v2, &v3);
  uint64_t tag = v0 ^ v1 ^ v2 ^ v3;

  out[0] = (uint8_t)(tag >> 0);
  out[1] = (uint8_t)(tag >> 8);
  out[2] = (uint8_t)(tag >> 16);
  out[3] = (uint8_t)(tag >> 24);
  out[4] = (uint8_t)(tag >> 32);
  out[5] = (uint8_t)(tag >> 40);
  out[6] = (uint8_t)(tag >> 48);
  out[7] = (uint8_t)(tag >> 56);
}
