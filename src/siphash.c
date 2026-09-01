/*
 * siphash.c - SipHash-2-4 (Aumasson & Bernstein), the Earshot message
 * authentication primitive (SPEC 5.2).
 *
 * 128-bit key, 64-bit output. c = 2 compression rounds, d = 4 finalisation
 * rounds. This is a straightforward port of the reference implementation;
 * integer-only, no tables, ~200 lines of object code.
 *
 * The key bytes and the message bytes are read in wire order; the 64-bit
 * result is serialised little-endian to form the 8-byte tag on the wire.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "earshot_internal.h"

static uint64_t load64_le(const uint8_t *p)
{
    return (uint64_t)p[0]        | (uint64_t)p[1] << 8  |
           (uint64_t)p[2] << 16  | (uint64_t)p[3] << 24 |
           (uint64_t)p[4] << 32  | (uint64_t)p[5] << 40 |
           (uint64_t)p[6] << 48  | (uint64_t)p[7] << 56;
}

#define ROTL(x, b) (uint64_t)(((x) << (b)) | ((x) >> (64 - (b))))

#define SIPROUND                                                     \
    do {                                                             \
        v0 += v1; v1 = ROTL(v1, 13); v1 ^= v0; v0 = ROTL(v0, 32);    \
        v2 += v3; v3 = ROTL(v3, 16); v3 ^= v2;                       \
        v0 += v3; v3 = ROTL(v3, 21); v3 ^= v0;                       \
        v2 += v1; v1 = ROTL(v1, 17); v1 ^= v2; v2 = ROTL(v2, 32);    \
    } while (0)

uint64_t esh_siphash24(const uint8_t key[EARSHOT_KEY_BYTES],
                       const uint8_t *msg, size_t n)
{
    uint64_t k0 = load64_le(key);
    uint64_t k1 = load64_le(key + 8);

    uint64_t v0 = 0x736f6d6570736575ULL ^ k0;
    uint64_t v1 = 0x646f72616e646f6dULL ^ k1;
    uint64_t v2 = 0x6c7967656e657261ULL ^ k0;
    uint64_t v3 = 0x7465646279746573ULL ^ k1;

    const size_t whole = n - (n % 8);
    uint64_t b = (uint64_t)n << 56;

    for (size_t i = 0; i < whole; i += 8) {
        uint64_t m = load64_le(msg + i);
        v3 ^= m;
        SIPROUND;
        SIPROUND;
        v0 ^= m;
    }

    for (size_t i = whole; i < n; i++)
        b |= (uint64_t)msg[i] << (8 * (i - whole));

    v3 ^= b;
    SIPROUND;
    SIPROUND;
    v0 ^= b;

    v2 ^= 0xff;
    SIPROUND;
    SIPROUND;
    SIPROUND;
    SIPROUND;

    return v0 ^ v1 ^ v2 ^ v3;
}

int esh_tag_equal(const uint8_t tag[EARSHOT_TAG_BYTES], uint64_t mac)
{
    uint8_t want[8];
    for (int i = 0; i < 8; i++)
        want[i] = (uint8_t)(mac >> (8 * i));

    uint8_t diff = 0;
    for (int i = 0; i < 8; i++)
        diff |= (uint8_t)(tag[i] ^ want[i]);

    return diff == 0;
}
