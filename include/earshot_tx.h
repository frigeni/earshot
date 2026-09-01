/*
 * earshot_tx.h - device-side transmitter for the Earshot return channel
 *               (Profile A: audible, device -> phone). Brief point 7.
 *
 * STATUS: NOT IMPLEMENTED. Profile A is not designed yet; see
 * spec/PROFILE-A.md. This header sketches the intended shape so the split
 * (no libm, no malloc, no floating point) is visible from the start.
 *
 * Constraints for the implementation in src/tx/:
 *   - no dynamic allocation
 *   - no floating point, no libm
 *   - no dependencies beyond src/siphash.c and src/crc.c (both integer-only)
 *   - must run on a small MCU, and when the device is half broken
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

/* Placeholder API. Expected surface:
 *
 *   void esh_tx_init(esh_tx_t *t, const uint8_t key[16], uint8_t keyid,
 *                    uint32_t counter, const uint8_t *msg, uint16_t len);
 *
 *   // Produce the next frame as a bit pattern for the square-wave driver:
 *   // which of the plan's tones are active this symbol. The caller clocks
 *   // this out on a GPIO with a hardware timer.
 *   int  esh_tx_next_symbol(esh_tx_t *t, uint16_t *tone_mask);
 *
 * The fountain encoder, envelope builder and (optional) SipHash tag are shared
 * in spirit with the receiver but re-implemented without floating point: the
 * robust-soliton CDF is a precomputed integer table for a bounded K range.
 */

#ifdef __cplusplus
}
#endif
#endif /* EARSHOT_TX_H */
