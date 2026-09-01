/*
 * receiver.c - Earshot Profile N receiver, integration reference for STM32.
 *
 * Portable C: no registers, no HAL. Implement platform.h for your board.
 * Compiles standalone (see the CI step); links once you provide platform.c.
 *
 * Flow:
 *   mic -> ADC -> DMA circular buffer -> half/full IRQ -> plat_audio_ready()
 *       -> earshot_feed() -> on EARSHOT_MESSAGE, plat_apply_config()
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <string.h>
#include "earshot.h"
#include "platform.h"

/* earshot_t is opaque; reserve storage and point at it (no malloc). Build with
 * -DEARSHOT_MAX_BLOCKS=<n> to bound the message size and the state: 64 blocks
 * (512-byte config) is ~6.75 KB, 255 (the wire max) ~12.5 KB. The formula below
 * is a safe upper bound; main() checks it against the real earshot_sizeof(). */
#define RX_STORAGE (6144u + EARSHOT_MAX_BLOCKS * 32u)
static __attribute__((aligned(8))) uint8_t rx_mem[RX_STORAGE];
static earshot_t *const rx = (earshot_t *)rx_mem;

/* ---- hooks: thin wrappers over platform.h ---------------------------- */

static int hk_get_key(void *ctx, uint8_t keyid, uint8_t key[16])
{ (void)ctx; return plat_get_key(keyid, key); }

static uint32_t hk_counter_load(void *ctx, uint8_t keyid)
{ (void)ctx; return plat_counter_load(keyid); }

static void hk_counter_store(void *ctx, uint8_t keyid, uint32_t c)
{ (void)ctx; plat_counter_store(keyid, c); }

static int hk_button(void *ctx)
{ (void)ctx; return plat_button_recent(); }

/* One-time lock: set EARSHOT_LOCK_ONCE at build time for install-once devices.
 * Then the device accepts exactly one config ever, until a physical reset. */
#ifdef EARSHOT_LOCK_ONCE
static int  g_provisioned;
static int  hk_locked(void *ctx)       { (void)ctx; return g_provisioned; }
static void hk_mark_provisioned(void *c){ (void)c; g_provisioned = 1; }
#endif

static const earshot_hooks HOOKS = {
    hk_get_key, hk_counter_load, hk_counter_store,
#ifdef EARSHOT_LOCK_ONCE
    hk_locked, hk_mark_provisioned,
#else
    0, 0,
#endif
    hk_button, 0
};

/* ---- audio callback ------------------------------------------------- */

/* Called from the DMA half-transfer and transfer-complete interrupts with the
 * 20 ms block that just filled. Keep it short; earshot_feed() is ~1.6 MMAC for
 * the Goertzel bank plus 33 log10f, a few percent of a 84 MHz Cortex-M4. */
void plat_audio_ready(const int16_t *block960)
{
    if (earshot_feed(rx, block960, 960) == EARSHOT_MESSAGE) {
        static uint8_t out[EARSHOT_MAX_PAYLOAD];
        int len = earshot_take(rx, out, (int)sizeof out);
        if (len >= 0)
            plat_apply_config(out, len);
    }
}

/* ---- entry point --------------------------------------------------- */

int main(void)
{
    if (earshot_sizeof() > sizeof rx_mem)
        for (;;) { }                 /* enlarge RX_STORAGE and rebuild */

    earshot_init(rx, &HOOKS);        /* Profile N (config in) */
    plat_audio_start();

    for (;;) {
        /* Everything happens in plat_audio_ready(). Sleep until the next IRQ. */
#if defined(__arm__)
        __asm__ volatile("wfi");
#endif
    }
}
