/*
 * transmitter.c - Earshot Profile A transmitter, integration reference.
 *
 * Drives one GPIO pin (through platform.h) as a square wave so the device
 * "sings" its diagnostics to a nearby phone. Integer only, no libm, ~280 bytes
 * of state. Small enough to call from a fault handler.
 *
 * Compiles standalone (see the CI step); links once you provide platform.c
 * and link against src/tx/earshot_tx.c, src/siphash.c, src/crc.c.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "earshot_tx.h"
#include "platform.h"

/* Profile A default plan (spec/PROFILE-A.md section 2). Retune BASE/STEP for
 * your buzzer's resonance, but keep the harmonic invariant:
 *     2 * SYNC_HZ  >  BASE_HZ + STEP_HZ * (16 - 1)
 * so no square-wave harmonic of any tone can land on another tone. */
#define ESH_A_SYNC_HZ   2300u
#define ESH_A_BASE_HZ   2600u
#define ESH_A_STEP_HZ    100u
#define ESH_A_TONE_MS     40u
#define ESH_A_GUARD_MS    20u

static earshot_tx_t g_tx;

/* Sing `report` on the buzzer, forever (the fountain loops; a phone that starts
 * listening at any point still recovers the whole message). Returns only if the
 * report is too long for this build. `boot_count` goes in the envelope counter
 * field - informational, the phone enforces no anti-replay on telemetry. */
void earshot_sing(const uint8_t key[16], uint8_t keyid, uint32_t boot_count,
                  const uint8_t *report, uint16_t len)
{
    if (earshot_tx_init(&g_tx, key, keyid, boot_count, report, len) != 0)
        return;

    for (;;) {
        int s = earshot_tx_next(&g_tx);
        uint32_t hz = (s == EARSHOT_TX_SYNC)
                        ? ESH_A_SYNC_HZ
                        : ESH_A_BASE_HZ + ESH_A_STEP_HZ * (uint32_t)s;
        plat_tone(hz);
        plat_delay_ms(ESH_A_TONE_MS);
        plat_idle();
        plat_delay_ms(ESH_A_GUARD_MS);
    }
}
