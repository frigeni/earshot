/*
 * fountain.c - LT fountain decoder (SPEC 4).
 *
 * Reconstructs the message envelope from coded frames. Each frame carries the
 * XOR of a pseudo-random subset of the K source blocks; the subset is derived
 * from the 16-bit seed alone, so the decoder needs nothing but the seed to know
 * what was combined.
 *
 * The degree distribution (robust soliton) is computed in `double` to stay
 * bit-identical with the JavaScript transmitter: a different rounding at a CDF
 * boundary would pick a different degree and the decode would diverge.
 *
 * The peeling decoder uses an explicit index FIFO, not recursion, so decode
 * depth cannot overflow a small stack.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <math.h>
#include <string.h>
#include "earshot_internal.h"

/* ---- xorshift32 PRNG (SPEC 4.1) ---------------------------------------- */

typedef struct { uint32_t s; } esh_rng;

static void rng_seed(esh_rng *r, uint32_t seed)
{
    r->s = seed ? seed : 0x9E3779B9u;
}

static double rng_next(esh_rng *r)
{
    uint32_t s = r->s;
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    r->s = s;
    return (double)s / 4294967296.0;
}

/* ---- robust soliton CDF (SPEC 4.2) ----------------------------------------- */

static void soliton(esh_fountain *f, int K)
{
    const double c = 0.05, delta = 0.05;
    const double R = c * log((double)K / delta) * sqrt((double)K);
    double *p = f->cdf;            /* scratch: p[1..K], then overwritten by CDF */
    int d;

    for (d = 1; d <= K; d++)
        p[d] = (d == 1) ? 1.0 / K : 1.0 / ((double)d * (d - 1));

    int kr = (int)floor((double)K / R + 0.5);
    if (kr < 1) kr = 1;
    for (d = 1; d < kr && d <= K; d++)
        p[d] += R / ((double)d * K);
    if (kr <= K)
        p[kr] += R * log(R / delta) / K;

    double tot = 0.0;
    for (d = 1; d <= K; d++) tot += p[d];

    double acc = 0.0;
    for (d = 1; d <= K; d++) { acc += p[d] / tot; f->cdf[d] = acc; }
    f->cdf[K] = 1.0;
    f->cdf_K = K;
}

static int degree(const esh_fountain *f, int K, double r)
{
    int lo = 1, hi = K;
    while (lo < hi) {
        int m = (lo + hi) >> 1;
        if (f->cdf[m] < r) lo = m + 1;
        else               hi = m;
    }
    return lo;
}

/* Reconstruct the block set for a seed. Writes a K-bit bitmap. Returns degree. */
static int block_set(const esh_fountain *f, uint32_t seed, int K, uint8_t *bitmap)
{
    esh_rng r;
    rng_seed(&r, seed);
    int d = degree(f, K, rng_next(&r));
    if (d > K) d = K;

    memset(bitmap, 0, (size_t)(K + 7) / 8);
    int n = 0;
    while (n < d) {
        int i = (int)floor(rng_next(&r) * K) % K;
        if (!(bitmap[i >> 3] & (1u << (i & 7)))) {
            bitmap[i >> 3] |= (uint8_t)(1u << (i & 7));
            n++;
        }
    }
    return d;
}

/* ---- decoder ------------------------------------------------------------- */

void esh_fountain_reset(esh_fountain *f)
{
    memset(f, 0, sizeof(*f));
}

static int bit_get(const uint8_t *bm, int i) { return (bm[i >> 3] >> (i & 7)) & 1; }
static void bit_clr(uint8_t *bm, int i)      { bm[i >> 3] &= (uint8_t)~(1u << (i & 7)); }

static int have_block(const esh_fountain *f, int i) { return bit_get(f->have, i); }

static void store_block(esh_fountain *f, int i, const uint8_t *val)
{
    memcpy(f->block[i], val, ESH_PAY);
    f->have[i >> 3] |= (uint8_t)(1u << (i & 7));
    f->done_count++;
}

/* Store block start_idx, then propagate through held frames. An explicit index
 * FIFO replaces recursion; the inner scan restarts after any held-array removal
 * so a shifted entry is never skipped. */
static void resolve(esh_fountain *f, int start_idx, const uint8_t *start_val)
{
    uint8_t queue[EARSHOT_MAX_BLOCKS];
    int qh = 0, qt = 0;

    if (have_block(f, start_idx)) return;
    store_block(f, start_idx, start_val);
    queue[qt++] = (uint8_t)start_idx;

    while (qh != qt) {
        int i = queue[qh++];
        int h = f->held_count - 1;
        while (h >= 0) {
            esh_held *e = &f->held[h];
            if (!bit_get(e->bitmap, i)) { h--; continue; }

            for (int j = 0; j < ESH_PAY; j++) e->data[j] ^= f->block[i][j];
            bit_clr(e->bitmap, i);
            e->remaining--;

            if (e->remaining == 1) {
                int k = -1;
                for (int b = 0; b < f->K; b++)
                    if (bit_get(e->bitmap, b)) { k = b; break; }
                uint8_t val[ESH_PAY];
                memcpy(val, e->data, ESH_PAY);
                *e = f->held[--f->held_count];        /* remove entry h */
                if (k >= 0 && !have_block(f, k)) {
                    store_block(f, k, val);
                    queue[qt++] = (uint8_t)k;
                }
                h = f->held_count - 1;                /* array shifted: restart */
            } else if (e->remaining <= 0) {
                *e = f->held[--f->held_count];
                h = f->held_count - 1;
            } else {
                h--;
            }
        }
    }

    if (f->K > 0 && f->done_count == f->K) f->complete = 1;
}

int esh_fountain_push(esh_fountain *f, const uint8_t frame[ESH_FRAME_BYTES])
{
    int K = frame[0];
    if (K < 1 || K > EARSHOT_MAX_BLOCKS) return 0;

    uint8_t chk[3 + ESH_PAY];
    chk[0] = frame[0]; chk[1] = frame[1]; chk[2] = frame[2];
    memcpy(chk + 3, frame + 4, ESH_PAY);
    if (esh_crc8(chk, sizeof chk) != frame[3]) return 0;

    if (f->K == 0) { f->K = K; soliton(f, K); }
    else if (f->K != K) return 0;

    f->frames_ok++;

    uint32_t seed = (uint32_t)frame[1] | ((uint32_t)frame[2] << 8);
    uint8_t bitmap[ESH_BITMAP_BYTES];
    block_set(f, seed, K, bitmap);

    uint8_t data[ESH_PAY];
    memcpy(data, frame + 4, ESH_PAY);

    int remaining = 0, last = -1;
    for (int i = 0; i < K; i++) {
        if (!bit_get(bitmap, i)) continue;
        if (have_block(f, i)) {
            for (int j = 0; j < ESH_PAY; j++) data[j] ^= f->block[i][j];
            bit_clr(bitmap, i);
        } else {
            remaining++;
            last = i;
        }
    }

    if (remaining == 1) {
        resolve(f, last, data);
    } else if (remaining > 1 && f->held_count < ESH_HOLD_MAX) {
        esh_held *e = &f->held[f->held_count++];
        memcpy(e->bitmap, bitmap, sizeof e->bitmap);
        memcpy(e->data, data, ESH_PAY);
        e->remaining = remaining;
    }
    return 1;
}

size_t esh_fountain_assemble(const esh_fountain *f, uint8_t *out)
{
    if (!f->complete) return 0;
    for (int i = 0; i < f->K; i++)
        memcpy(out + (size_t)i * ESH_PAY, f->block[i], ESH_PAY);
    return (size_t)f->K * ESH_PAY;
}
