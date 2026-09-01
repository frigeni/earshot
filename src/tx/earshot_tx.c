/*
 * earshot_tx.c - device-side transmitter (Profile A). spec/PROFILE-A.md.
 *
 * Integer only. Builds the signed envelope, fountain-codes it, and yields one
 * symbol per call. The robust-soliton degree comes from the fixed-point table
 * in soliton_table.h, generated and exhaustively checked against the
 * floating-point decoder by tools/soliton_table.py.
 *
 * Depends only on esh_siphash24 (src/siphash.c) and esh_crc8 / esh_crc16
 * (src/crc.c), both integer-only.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdint.h>
#include <string.h>
#include "earshot_tx.h"
#include "soliton_table.h"

/* the only external dependencies */
uint64_t esh_siphash24(const uint8_t key[16], const uint8_t *msg, size_t n);
uint8_t  esh_crc8(const uint8_t *b, size_t n);
uint16_t esh_crc16(const uint8_t *b, size_t n);

#define PAY 8

static uint32_t xs32(uint32_t s)
{
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
}

static void build_envelope(earshot_tx_t *t, const uint8_t key[16], uint8_t keyid,
                           uint32_t counter, const uint8_t *msg, uint16_t len)
{
    uint8_t *e = t->env;
    e[0] = (uint8_t)((1u << 4) | (keyid & 0x0Fu));      /* ver 1 | keyid */
    e[1] = (uint8_t)(counter >> 24);
    e[2] = (uint8_t)(counter >> 16);
    e[3] = (uint8_t)(counter >> 8);
    e[4] = (uint8_t)counter;
    e[5] = (uint8_t)(len >> 8);
    e[6] = (uint8_t)len;
    memcpy(e + 7, msg, len);

    size_t signed_len = (size_t)7 + len;
    uint64_t mac = esh_siphash24(key, e, signed_len);
    for (int i = 0; i < 8; i++)
        e[signed_len + i] = (uint8_t)(mac >> (8 * i));

    uint16_t crc = esh_crc16(e, signed_len + 8);
    e[signed_len + 8] = (uint8_t)(crc >> 8);
    e[signed_len + 9] = (uint8_t)crc;

    size_t total  = signed_len + 10;
    size_t padded = (size_t)t->K * PAY;
    while (total < padded)
        e[total++] = 0;
}

int earshot_tx_init(earshot_tx_t *t, const uint8_t key[16], uint8_t keyid,
                    uint32_t counter, const uint8_t *msg, uint16_t len)
{
    memset(t, 0, sizeof *t);
    if (len < 1)
        return -1;

    int K = (len + 17 + (PAY - 1)) / PAY;
    if (K < ESH_SOLITON_KMIN || K > ESH_SOLITON_KMAX || K > EARSHOT_TX_MAX_BLOCKS)
        return -1;

    t->K   = K;
    t->seq = 0;
    t->sym = -1;
    build_envelope(t, key, keyid, counter, msg, len);
    return 0;
}

static void make_frame(earshot_tx_t *t)
{
    t->seq++;
    uint32_t seed = (t->seq * 40503u) & 0xFFFFu;
    if (seed == 0)
        seed = 1;

    uint32_t s = xs32(seed);
    const uint32_t *cdf = esh_soliton_cdf[t->K - ESH_SOLITON_KMIN];
    int lo = 1, hi = t->K;
    while (lo < hi) {
        int m = (lo + hi) >> 1;
        if (cdf[m] < s) lo = m + 1;
        else            hi = m;
    }
    int d = lo;
    if (d > t->K) d = t->K;

    uint8_t payload[PAY];
    uint8_t seen[(EARSHOT_TX_MAX_BLOCKS + 7) / 8];
    memset(payload, 0, sizeof payload);
    memset(seen, 0, sizeof seen);

    int got = 0;
    while (got < d) {
        s = xs32(s);
        int i = (int)(((uint64_t)s * (uint32_t)t->K) >> 32);
        if (!(seen[i >> 3] & (1u << (i & 7)))) {
            seen[i >> 3] |= (uint8_t)(1u << (i & 7));
            for (int j = 0; j < PAY; j++)
                payload[j] ^= t->env[i * PAY + j];
            got++;
        }
    }

    t->frame[0] = (uint8_t)t->K;
    t->frame[1] = (uint8_t)(seed & 0xFF);
    t->frame[2] = (uint8_t)(seed >> 8);
    memcpy(t->frame + 4, payload, PAY);

    uint8_t chk[3 + PAY];
    chk[0] = t->frame[0];
    chk[1] = t->frame[1];
    chk[2] = t->frame[2];
    memcpy(chk + 3, payload, PAY);
    t->frame[3] = esh_crc8(chk, sizeof chk);
}

int earshot_tx_next(earshot_tx_t *t)
{
    if (t->sym < 0) {
        make_frame(t);
        t->sym = 0;
        return EARSHOT_TX_SYNC;
    }

    int idx = t->sym;
    uint8_t byte = t->frame[idx >> 1];
    int nibble = (idx & 1) ? (byte >> 4) : (byte & 0x0F);

    if (++t->sym >= 24)
        t->sym = -1;
    return nibble;
}
