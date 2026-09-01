/*
 * platform.h - the integration surface for the STM32 receiver example.
 *
 * Implement these for your board. The example (receiver.c) is portable C and
 * does not touch a single register; a real port fills these in with HAL or
 * bare-metal code. See README.md for an STM32F411 sketch.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef EARSHOT_EXAMPLE_PLATFORM_H
#define EARSHOT_EXAMPLE_PLATFORM_H

#include <stdint.h>

/* Start the microphone -> ADC -> DMA chain at 48 kHz, mono, into a circular
 * buffer. Call plat_audio_ready() from both the half-transfer and the
 * transfer-complete interrupt with a pointer to the 960-sample (20 ms) block
 * that just filled and is now safe to read. */
void plat_audio_start(void);
void plat_audio_ready(const int16_t *block960);   /* provided by receiver.c */

/* Milliseconds since boot (any monotonic source; SysTick is fine). */
uint32_t plat_millis(void);

/* Non-volatile store for the per-keyid replay counter (SPEC 5.4). Two-slot,
 * power-loss safe. Return 0 if nothing has been stored for this keyid. */
uint32_t plat_counter_load(uint8_t keyid);
void     plat_counter_store(uint8_t keyid, uint32_t counter);

/* 1 if the provisioning button was pressed within the last 60 s. A board with
 * no button returns 0 (the 60 s power-on window still applies). */
int plat_button_recent(void);

/* Look up the pre-shared key for keyid. Return 1 and fill key[16], or 0. */
int plat_get_key(uint8_t keyid, uint8_t key[16]);

/* Apply a validated configuration payload. */
void plat_apply_config(const uint8_t *data, int len);

#endif /* EARSHOT_EXAMPLE_PLATFORM_H */
