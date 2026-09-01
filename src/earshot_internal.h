/*
 * earshot_internal.h - declarations shared between the receiver source files.
 * Not a public interface. Do not install.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef EARSHOT_INTERNAL_H
#define EARSHOT_INTERNAL_H

#include <stdint.h>
#include <stddef.h>
#include "earshot.h"

/* ---- channel constants (SPEC 3, 4) --------------------------------------- */

#define ESH_SR            48000
#define ESH_SUBBLOCK      960          /* 20 ms                              */
#define ESH_SUB_PER_SYM   4            /* sub-blocks per 80 ms symbol        */
#define ESH_SYMS_PER_FRM  13           /* 1 sync + 12 data                   */
#define ESH_FRAME_BYTES   12           /* K, seed_lo, seed_hi, crc8, 8 pay   */
#define ESH_PAY           8            /* fountain block size                */
#define ESH_TONES         16           /* tones per lane                     */

#define ESH_F_SYNC        16400.0f
#define ESH_F_LANE_A      16700.0f     /* low nibble                         */
#define ESH_F_LANE_B      17900.0f     /* high nibble                        */
#define ESH_F_STEP        60.0f

/* detection thresholds, dB (SPEC 3.5) */
#define ESH_SYNC_DB       9.0f
#define ESH_TONE_DB       7.0f
#define ESH_MARGIN_DB     4.0f

/* provisioning window in samples (SPEC 5.5) */
#define ESH_PRESENCE_SAMPLES \
    ((uint64_t)ESH_SR * EARSHOT_PRESENCE_WINDOW_MS / 1000u)

/* upper bound on frames held unresolved by the peeling decoder (SPEC 4.4) */
#define ESH_HOLD_MAX      48

/* envelope geometry (SPEC 5.1) */
#define ESH_ENV_HDR       1
#define ESH_ENV_COUNTER   4
#define ESH_ENV_LEN       2
#define ESH_ENV_TAG       8
#define ESH_ENV_CRC       2
#define ESH_ENV_PREFIX    (ESH_ENV_HDR + ESH_ENV_COUNTER + ESH_ENV_LEN)  /* 7  */
#define ESH_ENV_SUFFIX    (ESH_ENV_TAG + ESH_ENV_CRC)                    /* 10 */
#define ESH_ENV_OVERHEAD  (ESH_ENV_PREFIX + ESH_ENV_SUFFIX)             /* 17 */

#define ESH_BITMAP_BYTES  ((EARSHOT_MAX_BLOCKS + 7) / 8)
#define ESH_ENV_MAX       (EARSHOT_MAX_BLOCKS * ESH_PAY)

/* ---- crc.c ------------------------------------------------------------- */

uint8_t  esh_crc8 (const uint8_t *b, size_t n);   /* poly 0x07, init 0x00      */
uint16_t esh_crc16(const uint8_t *b, size_t n);   /* CRC-16/CCITT-FALSE        */

/* ---- siphash.c ------------------------------------------------------------ */

uint64_t esh_siphash24(const uint8_t key[EARSHOT_KEY_BYTES],
                       const uint8_t *msg, size_t n);

/* Constant-time equality of an 8-byte tag against the low 64 bits of `mac`,
 * interpreted little-endian. Returns 1 on match, 0 otherwise. */
int esh_tag_equal(const uint8_t tag[EARSHOT_TAG_BYTES], uint64_t mac);

/* ---- fountain.c --------------------------------------------------------- */

typedef struct {
    uint8_t  bitmap[ESH_BITMAP_BYTES];
    uint8_t  data[ESH_PAY];
    int      remaining;
} esh_held;

typedef struct {
    int      K;                                   /* 0 until learned          */
    int      done_count;
    int      frames_ok;
    int      complete;
    uint8_t  have[ESH_BITMAP_BYTES];
    uint8_t  block[EARSHOT_MAX_BLOCKS][ESH_PAY];
    esh_held held[ESH_HOLD_MAX];
    int      held_count;
    double   cdf[EARSHOT_MAX_BLOCKS + 1];
    int      cdf_K;
} esh_fountain;

void esh_fountain_reset(esh_fountain *f);

/* Feed one validated 12-byte frame (crc8 already checked by the caller is not
 * assumed: this function checks it). Returns 1 if the frame was accepted into
 * the decode, 0 if it was rejected (bad crc8, K out of range, K mismatch). */
int  esh_fountain_push(esh_fountain *f, const uint8_t frame[ESH_FRAME_BYTES]);

/* Assemble the reconstructed envelope. Returns the number of bytes written
 * (K * 8), or 0 if not complete. `out` must hold at least ESH_ENV_MAX bytes. */
size_t esh_fountain_assemble(const esh_fountain *f, uint8_t *out);

/* ---- envelope.c ------------------------------------------------------------ */

/* Parse and authenticate an assembled envelope, then apply the SPEC 5.4
 * acceptance rule using the hooks. On EARSHOT_OK, *out_len / out_data point at
 * the payload inside `env`, and *keyid / *counter carry the accepted values.
 * `presence` is 1 if the power-on provisioning window is still open. */
earshot_reject esh_envelope_check(const uint8_t *env, size_t env_len,
                                  const earshot_hooks *h, int presence,
                                  const uint8_t **out_data, int *out_len,
                                  uint8_t *keyid, uint32_t *counter);

#endif /* EARSHOT_INTERNAL_H */
