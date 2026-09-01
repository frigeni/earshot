/*
 * earshot_tx.h - device-side transmitter for the Earshot return channel
 *               (Profile A: audible, device -> phone). Brief point 7,
 *               spec/PROFILE-A.md.
 *
 * Integer only: no libm, no floating point, no dynamic allocation. Links
 * against src/siphash.c and src/crc.c and nothing else. Small enough to run
 * from a fault handler so a half-broken device can still report why it died.
 *
 * Usage: the caller owns the timer. earshot_tx_next() returns the next symbol;
 * the caller maps it to a frequency and drives one GPIO pin as a square wave
 * for tone_ms, then holds the pin idle for guard_ms.
 *
 *     earshot_tx_t tx;
 *     earshot_tx_init(&tx, key, keyid, boot_count, report, report_len);
 *     for (;;) {
 *         int s = earshot_tx_next(&tx);
 *         uint32_t hz = (s == EARSHOT_TX_SYNC) ? 2300 : 2600 + 100 * s;
 *         gpio_square_wave(hz, 40);   // ms
 *         gpio_idle(20);
 *     }
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef EARSHOT_TX_H
#define EARSHOT_TX_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Largest message this build can transmit, in 8-byte blocks. Must not exceed
 * ESH_SOLITON_KMAX in src/tx/soliton_table.h. Lower it to shrink the state. */
#ifndef EARSHOT_TX_MAX_BLOCKS
#define EARSHOT_TX_MAX_BLOCKS 32
#endif

/* earshot_tx_next() returns this at the start of every frame. */
#define EARSHOT_TX_SYNC (-1)

/* State. Placed by the caller; treat the fields as opaque. */
typedef struct earshot_tx {
    uint8_t  env[EARSHOT_TX_MAX_BLOCKS * 8];
    int      K;
    uint32_t seq;
    int      sym;                 /* -1 = emit sync next, else 0..23 */
    uint8_t  frame[12];
} earshot_tx_t;

/* Build the signed envelope for `msg` and prime the fountain. `msg` is copied.
 * `counter` is the device boot count (informational; the phone does not enforce
 * anti-replay on telemetry). Returns 0, or -1 if `len` is out of range for this
 * build (1 .. EARSHOT_TX_MAX_BLOCKS*8 - 17, and K within the soliton table). */
int earshot_tx_init(earshot_tx_t *t, const uint8_t key[16], uint8_t keyid,
                    uint32_t counter, const uint8_t *msg, uint16_t len);

/* Next symbol to emit: EARSHOT_TX_SYNC once per frame, then 0..15 for the 24
 * data nibbles, then the next frame, forever. */
int earshot_tx_next(earshot_tx_t *t);

#ifdef __cplusplus
}
#endif
#endif /* EARSHOT_TX_H */
