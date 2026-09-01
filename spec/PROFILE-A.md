# Profile A — audible return channel (device → phone)

**Version 1 (draft) — 2026-09-01**

Profile A is the second direction: the device transmits diagnostics (error code,
serial number, firmware version, running hours, recent events) by driving the
piezo buzzer it usually already has, and the phone receives it with its
microphone. Brief point 7.

Everything above the physical layer is **unchanged from `SPEC.md`**: same
message envelope (§5), same CRCs, same fountain code (§4), same receiver state
machine. Only the frequency plan, the modulation and the waveform synthesis
differ. This document specifies those, and the parts of the envelope that
Profile A uses differently.

---

## 1. Why single-tone MFSK

Profile N uses two simultaneous tones per symbol (lane A + lane B = one byte)
because its transmitter is Web Audio and can sum two oscillators. The Profile A
transmitter is **one GPIO pin driven by a hardware timer**: it produces a square
wave at exactly one frequency at a time. It cannot emit two tones together.

So Profile A is **single-tone MFSK**: one symbol carries one tone chosen from
`M` = one nibble. Two symbols per byte. A side benefit: with only one tone on
air at a time there is no intermodulation between our own tones (the third-order
product `2·f_i − f_j`, the only IMD term that can land back in band, cannot
occur).

---

## 2. Frequency plan

### 2.1 The design invariant

The buzzer emits a square wave, so driving it at fundamental `f` also radiates
`2f` (when the duty cycle is not exactly 50 %), `3f`, `5f`, `7f`… The phone's
microphone hears all of them. If a harmonic of one tone lands on another tone of
the plan, the receiver cannot tell "3rd harmonic of `f_i`" from "`f_j` was
transmitted".

**Invariant: `2 · f_min > f_max`** — the whole plan (sync included) spans less
than one octave. Then for every tone `f` and every harmonic `n ≥ 2`:

```
n·f  ≥  2·f  ≥  2·f_min  >  f_max
```

so **every harmonic of every tone lands above the entire plan** and cannot be
confused with a fundamental. Any integrator who retunes the plan for a different
buzzer MUST preserve this invariant.

### 2.2 The default plan

| purpose | frequency |
|---|---|
| sync | 2300 Hz |
| data tones | 2600 + 100·k Hz, k = 0..15 (M = 16) → 2600, 2700, …, 4100 |

- Band occupied: **[2300, 4100] Hz**, centred on the 3–4 kHz resonance of a
  typical 20–35 mm piezo element.
- `2 · f_min = 4600 Hz`, which is **500 Hz above** `f_max = 4100 Hz` — margin
  enough that a ±2 % tone-frequency error still keeps every harmonic out of band.

### 2.3 Harmonic check

`f_min = 2300` (sync), `f_max = 4100`, `2·f_min = 4600`.

| tone | 2f | 3f | 5f | 7f | in [2300, 4100]? |
|---|---|---|---|---|---|
| 2300 (sync) | 4600 | 6900 | 11500 | 16100 | no |
| 2600 | 5200 | 7800 | 13000 | 18200 | no |
| 3300 | 6600 | 9900 | 16500 | 23100 | no |
| 4100 | 8200 | 12300 | 20500 | 28700 | no |

The lowest harmonic of any tone is `2 × 2300 = 4600 > 4100`. **No harmonic of
any tone falls in the plan — zero collisions, by construction.**

A wider plan would not: at a full octave (2000–4000) the 2nd harmonic of the
bottom tone lands exactly on the top tone; past an octave the 3rd harmonics
start colliding too. The sub-octave choice is what makes the harmonic question
vacuous.

### 2.4 Note: 5th harmonics reach the Profile N band

The 5th harmonics of Profile A tones fall at 11.5–20.5 kHz, crossing the
Profile N band (16.4–18.8 kHz). This only matters if Profile A and Profile N
transmit at the same time near the same devices; treat that as unsupported.

---

## 3. Modulation and timing

| parameter | default | notes |
|---|---|---|
| tones `M` | 16 → 1 nibble/symbol | |
| nibble order | low nibble first, then high | matches lane A = low nibble in Profile N |
| tone duration | 40 ms (two 20 ms sub-blocks) | |
| guard (silence) | 20 ms (one sub-block) | lets room reverberation decay |
| symbol | 60 ms = 3 sub-blocks | |
| detection | the receiver integrates the 2nd sub-block (20–40 ms into the tone); bin width ~50 Hz, well under the 100 Hz tone spacing |
| frame | 1 sync symbol + 24 data symbols = **12 bytes** | same frame payload as Profile N |
| throughput | 25 symbols × 60 ms = 1.5 s/frame → ~27 s for a 50-byte diagnostic (K ≈ 9, ~18 frames) | speed does not matter here |

The transmitter emits a **square wave** (50 % duty when the timer period is even;
a one-count asymmetry on odd periods is acceptable). During the guard interval
the pin is held at a constant level.

The sync symbol is the sync tone alone, present only in the first symbol of each
frame. Its frequency (2300 Hz for Profile A, 16400 Hz for Profile N) tells the
receiver which profile is on air — the frame carries no profile field.

---

## 4. Envelope use

Profile A uses the `SPEC.md` §5 envelope unchanged:

- **Always SipHash-signed** with the pre-shared key. The tag is cheap on the
  device (integer-only, ~200 bytes of code) and keeps one envelope format.
- `counter` carries the **device boot count** — informational, not anti-replay.
  The phone verifies the tag but enforces no monotonicity on received
  diagnostics (it takes no action on telemetry).
- `keyid` selects the key as in Profile N.
- `data` is the diagnostic payload; its internal structure is application
  defined and out of scope here.

---

## 5. Integer fountain code (device encoder)

The phone-side decoder computes the robust-soliton CDF in `double`. The device
encoder must not use floating point, and must still pick **bit-identical** block
sets or the decode diverges.

- **PRNG** (xorshift32) is already integer.
- **Index selection**: `floor(rng_draw · K)` = `((uint64_t)s · K) >> 32`, an
  exact identity for all `s`, `K`.
- **Degree selection** compares the PRNG draw against the CDF:
  `s / 2^32 < cdf[m]` ⟺ `s < round(cdf[m] · 2^32)`. The device ships a
  precomputed table `cdf_int[m] = round(cdf_float[m] · 2^32)` (`uint32`, capped
  at `2^32 − 1`).
- The 16-bit `seed` has only 65536 possible values. `tools/soliton_table.py`
  generates the table for the device's `K` range and **exhaustively verifies**
  that all 65536 seeds produce the same block set as the float reference for
  each `K`; a boundary entry is nudged by one ULP if any seed diverges.
- Table size ≈ 32 bytes per `K`. A fixed-format diagnostic uses one row.

---

## 6. `earshot_profile_t`

```c
typedef struct {
    uint16_t sync_hz;
    uint16_t base_hz;
    uint16_t step_hz;
    uint8_t  tones;      /* M */
    uint8_t  lanes;      /* 1 = Profile A (MFSK), 2 = Profile N */
    uint16_t tone_ms;
    uint16_t guard_ms;   /* 0 for Profile N */
} earshot_profile_t;
```

```c
typedef struct {
    uint16_t sync_hz;
    uint16_t lane_hz[2];    /* base of each lane; [1] unused if lanes == 1 */
    uint16_t step_hz;
    uint8_t  tones;         /* M */
    uint8_t  lanes;         /* 1 = Profile A, 2 = Profile N */
    uint8_t  sub_per_symbol;/* 20 ms sub-blocks per symbol */
    uint8_t  sample_first;  /* first sub-block to integrate */
    uint8_t  sample_last;   /* last (inclusive) */
    uint8_t  data_symbols;  /* symbols after sync */
} earshot_profile_t;
```

- **Profile N**: `{ 16400, {16700, 17900}, 60, 16, 2, 4, 1, 2, 12 }`.
- **Profile A**: `{ 2300, {2600, 0}, 100, 16, 1, 3, 1, 1, 24 }`.

An integrator retuning the plan for a different buzzer resonance keeps
`2·sync_hz > base_hz + step_hz·(tones−1)` (the harmonic invariant, §2.1).

---

## 7. Device transmitter constraints (brief point 7)

A minimal C file in `src/tx/` that drives one GPIO pin with a timer. No
dependencies beyond `src/siphash.c` and `src/crc.c` (both integer-only), no
dynamic allocation, no floating point. It must run on a small microcontroller,
and when the device is half broken — for example from a fault handler, so a
device can still report why it died.

---

## 8. Test bench (brief point 7)

`tools/` and `tests/` synthesise a square wave (fundamental + 3rd/5th/7th at
1/3, 1/5, 1/7, optional 2nd for duty error), a buzzer resonance filter, a
Schroeder reverberator (reverberation dominates at 2–4 kHz), and wideband
industrial-style noise. The bench measures where the decoder fails; the symbol
is lengthened if needed.
