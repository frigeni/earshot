# Piezo buzzer transmitter — Profile A

How to make a device "sing" its diagnostics (brief point 7, `spec/PROFILE-A.md`).
[`transmitter.c`](transmitter.c) is portable integer-only C;
[`platform.h`](platform.h) is three functions you implement.

## 1. The buzzer

A **bare piezo element** or a **piezo transducer** (the kind *without* a
built-in oscillator — not a "piezo sounder"/"active buzzer"). It has a
mechanical resonance, usually 2–4.5 kHz depending on the disc size:

| element | typical resonance |
|---|---|
| 12 mm disc | 6–7 kHz |
| 20 mm disc | 4–4.5 kHz |
| 27–35 mm disc / housed transducer | 2.5–3.5 kHz |

The default plan (sync 2300 Hz, tones 2600–4100 Hz) targets the 3–4 kHz group.
For a different element, retune `ESH_A_BASE_HZ` / `ESH_A_STEP_HZ` in
`transmitter.c` and keep the harmonic invariant
`2·SYNC_HZ > BASE_HZ + STEP_HZ·15` (see `spec/PROFILE-A.md` §2.1). Off
resonance the output drops ~15 dB, so keep the whole plan within the resonant
region.

## 2. Driving it

One GPIO pin, square wave, ~50 % duty. Two ways:

- **Direct GPIO** — pin straight to the piezo (other leg to GND). Fine for
  short range; a piezo is a ~10–20 nF capacitive load, within a GPIO's drive.
- **Transistor / half-bridge** — for more SPL, switch the piezo between a
  higher voltage rail and GND, or drive it differentially from two pins
  (antiphase) for ~2× the voltage swing. `plat_tone()` still just sets one
  frequency.

Best done with a hardware timer in **toggle-on-compare-match** mode: set the
compare register for `timer_clk / (2·hz)`, the pin flips every match, and the
CPU is free during the tone. `plat_tone(hz)` reloads the compare value;
`plat_idle()` stops the timer and parks the pin.

```c
/* STM32 TIM3 CH1, toggle mode, sketch */
void plat_tone(uint32_t hz) {
    uint32_t half = (TIMER_CLK / 2u) / hz;      /* integer; ~0.5 % worst case */
    __HAL_TIM_SET_AUTORELOAD(&htim3, half - 1);
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, 0);
    HAL_TIM_OC_Start(&htim3, TIM_CHANNEL_1);
}
void plat_idle(void) { HAL_TIM_OC_Stop(&htim3, TIM_CHANNEL_1); /* park low */ }
```

Frequency accuracy: with a 48 MHz timer clock, `half` for 4100 Hz is ~5854
counts → error < 0.02 %. Even a slow 1 MHz timer clock gives < 0.5 %, inside
the 500 Hz harmonic margin.

## 3. Calling it

```c
extern void earshot_sing(const uint8_t key[16], uint8_t keyid,
                         uint32_t boot_count, const uint8_t *report, uint16_t len);

/* normal operation */
uint8_t report[64];
int n = build_diagnostic(report);          /* your format: error code, serial, ... */
earshot_sing(DEVICE_KEY, 0, boot_count, report, n);

/* from a fault handler - the device is half dead but can still say why */
void HardFault_Handler(void) {
    static const uint8_t last_words[] = { 0xDE, 0xAD, /* fault code, PC, ... */ };
    earshot_sing(DEVICE_KEY, 0, boot_count, last_words, sizeof last_words);
}
```

`earshot_sing()` never returns (the fountain loops forever, so a phone that
starts listening late still gets the whole report). It uses ~280 bytes of
static RAM, no heap, no floating point. SipHash and the CRCs are integer table-
free code; the robust-soliton degree comes from `src/tx/soliton_table.h`.

## 4. Budget (measured)

`arm-none-eabi-gcc -Os`, Cortex-M0+:

| item | size |
|---|---|
| `earshot_tx.o` (incl. the K = 1..32 soliton table, ~4.3 KB rodata) | 4772 B |
| `siphash.o` | ~1.1 KB |
| `crc.o` | 94 B |
| `earshot_tx_t` state | 280 B RAM |

Regenerate the table for just your report's `K`
(`python3 tools/soliton_table.py --kmax <K>`) to drop `earshot_tx.o` to a few
hundred bytes.

## 5. Build

```
arm-none-eabi-gcc -Os -std=c99 -mcpu=cortex-m0plus -mthumb \
  -I../../include -I../../src/tx -I. \
  ../../src/tx/earshot_tx.c ../../src/siphash.c ../../src/crc.c \
  transmitter.c platform_stm32.c \
  -o earshot-tx.elf
```
