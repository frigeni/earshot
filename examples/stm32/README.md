# STM32 integration — Profile N receiver

How to run the Earshot receiver on an STM32. The example is a
CubeMX/HAL-independent reference: [`receiver.c`](receiver.c) is portable C,
[`platform.h`](platform.h) is the ~7-function surface you implement for your
board. A concrete STM32F411 sketch is below.

## 1. Microphone — the one that matters

**Most cheap MEMS microphones cannot hear Profile N.** They ship with an
anti-alias / anti-aliasing filter tuned for voice that rolls off hard above
~16 kHz, and Profile N lives at 16.4–18.8 kHz. Check the datasheet frequency
response *to 20 kHz*, not just "flat 100 Hz – 10 kHz".

| type | notes |
|---|---|
| **Analog MEMS** (e.g. Knowles SPH8878LR5H-1, TDK ICS-40740) | response usable to 20 kHz; feeds an ADC channel directly. Simplest path, and what this example assumes. |
| **Analog electret + op-amp** | fine if the capsule reaches 20 kHz (many do) and the op-amp bandwidth is ≥ 100 kHz. |
| **PDM MEMS** | works acoustically, but you need a PDM→PCM decimation filter (CIC + FIR) before `earshot_feed`. More CPU and more code; only worth it if the board already has a PDM mic. |
| **Analog MEMS with "RF/EMI filter" or "wind noise reduction"** | often a 16 kHz low-pass in disguise. Avoid. |

Bias the analog mic output to VDD/2 so the ADC sees the full swing.

## 2. Acquisition — ADC in DMA at 48 kHz

The receiver wants a steady 48 kHz mono stream of `int16`. On an STM32F411:

- **Timebase**: TIM2, `ARR` for 48 kHz update, `TRGO = update`.
- **ADC1**: one channel, 12-bit, `EXTSEL = TIM2 TRGO`, external trigger on
  rising edge, sample time 15 cycles (28 total ≈ 0.65 µs at ADCCLK = 42 MHz →
  comfortably inside the 20.8 µs budget).
- **DMA2 Stream0, Channel0** (ADC1): circular, peripheral→memory, half-word,
  buffer of **1920 samples** (two 20 ms blocks), **half-transfer + transfer-
  complete interrupts** enabled.
- In each IRQ: take the 960-sample half that just filled, convert
  `int16 = (int16_t)((raw - 2048) << 4)` in place (or into a scratch block),
  and call `plat_audio_ready(block)`.

```c
/* HAL flavour */
void HAL_ADC_ConvHalfCpltCallback(ADC_HandleTypeDef *h) { feed(&dma_buf[0]); }
void HAL_ADC_ConvCpltCallback    (ADC_HandleTypeDef *h) { feed(&dma_buf[960]); }

static int16_t block[960];
static void feed(const uint16_t *raw) {
    for (int i = 0; i < 960; i++) block[i] = (int16_t)(((int)raw[i] - 2048) << 4);
    plat_audio_ready(block);
}
```

Sample-rate accuracy: the transmitter (a phone) and your crystal will not agree
exactly. A ±0.5 % ceramic resonator shifts a 16.7 kHz tone by ~83 Hz, more than
half the 60 Hz bin spacing — use a crystal (±20–50 ppm), not the internal RC.

## 3. Non-volatile counter

`plat_counter_load` / `plat_counter_store` hold the per-keyid replay counter
(SPEC 5.4). It changes only when a config is accepted — rare — so endurance is a
non-issue, but a power loss mid-write must not corrupt it:

- **Two slots** with an incrementing sequence tag; read the higher sequence,
  write the other slot, then it becomes current.
- On an F411 with no EEPROM: a dedicated flash sector, or the two-slot pattern
  in the last two flash pages. On an L-series: real EEPROM emulation.

The physical reset (button held at boot, or a jumper) must clear these slots and
the `EARSHOT_LOCK_ONCE` flag if you use it.

## 4. Button

`plat_button_recent()` returns 1 for 60 s after a press. Wire the button to an
EXTI line, timestamp the press with `plat_millis()`, compare in the callback. A
board with no button returns 0 and relies on the 60 s power-on window for first
provisioning (SPEC 5.5).

## 5. Budget (measured)

`arm-none-eabi-gcc -Os`, from CI:

| item | size |
|---|---|
| `src/earshot.o` | 1428 B |
| `src/fountain.o` | 1468 B |
| `src/siphash.o` | 1144 B |
| `src/envelope.o` | 276 B |
| `src/crc.o` | 94 B |
| **Earshot code** | **~4.4 KB** flash |
| libm (`cosf`, `log10f`, `logf`, `sqrtf`) | ~3–6 KB, toolchain dependent |

| `EARSHOT_MAX_BLOCKS` | `earshot_t` RAM | max config |
|---|---|---|
| 32 | 5.8 KB | 239 B |
| 64 | 6.75 KB | 495 B |
| 255 (default) | 12.5 KB | 2023 B |

Plus the DMA buffer (1920 × 2 B) and a 960-sample scratch block ≈ 5.6 KB.

**CPU**: the Goertzel bank is `33 bins × 960` ≈ 31.7 kMAC per 20 ms block =
**1.6 MMAC/s**, plus 33 `log10f` per block. On an 84 MHz Cortex-M4F that is a
few percent. The robust-soliton table is built once, in `double` (software on
the F411's single-precision FPU) — a few thousand ops, one time, when `K` is
first learned.

## 6. Wiring (STM32F411, analog MEMS)

| signal | pin | note |
|---|---|---|
| mic OUT | PA0 (ADC1_IN0) | biased to VDD/2 |
| mic VDD / GND | 3V3 / GND | 100 nF close to the capsule |
| provisioning button | PC13 → GND | internal pull-up, EXTI |
| status LED | any GPIO | optional: blink on `EARSHOT_MESSAGE` |
| reset jumper | any GPIO, read at boot | clears the counter store |

## 7. Build

```
arm-none-eabi-gcc -Os -std=c99 -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard \
  -DEARSHOT_MAX_BLOCKS=64 \
  -I../../include -I. \
  ../../src/earshot.c ../../src/fountain.c ../../src/envelope.c \
  ../../src/siphash.c ../../src/crc.c \
  receiver.c platform_stm32f411.c \
  -o earshot-rx.elf
```

`platform_stm32f411.c` (your ADC/DMA/flash/EXTI code implementing `platform.h`)
is the only board-specific file.
