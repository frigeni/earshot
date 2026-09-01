/*
 * platform.h - the integration surface for the piezo buzzer transmitter.
 *
 * Three functions. No floating point. plat_tone() typically reloads a timer
 * running in toggle-on-match so one GPIO pin swings as a ~50% square wave.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef EARSHOT_BUZZER_PLATFORM_H
#define EARSHOT_BUZZER_PLATFORM_H

#include <stdint.h>

/* Drive the buzzer pin as a square wave at `hz` (2300..4100 for the default
 * Profile A plan). Returns immediately; the timer keeps toggling the pin. */
void plat_tone(uint32_t hz);

/* Stop toggling; hold the pin at a constant level (the guard interval). */
void plat_idle(void);

/* Busy-wait or sleep for `ms` milliseconds. */
void plat_delay_ms(uint32_t ms);

#endif /* EARSHOT_BUZZER_PLATFORM_H */
