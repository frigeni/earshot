# Changelog

All notable changes to Earshot are recorded here. The protocol version is the
`ver` field in the message envelope (`spec/SPEC.md` §5.1).

## [Unreleased]

### Added
- Protocol specification `spec/SPEC.md` (draft, protocol `ver = 1`).
- Portable C receiver, self-contained web transmitter + receiver, Python tools,
  end-to-end bench and unit vectors run in CI (gcc, clang, `arm-none-eabi`,
  address + undefined sanitizers).
- `examples/stm32/` — receiver integration reference (mic selection, ADC+DMA at
  48 kHz, the NVM counter, budgets) and `examples/buzzer/` — Profile A
  transmitter integration (piezo drive, timer square wave, calling from a fault
  handler). Both compile for their targets in CI.
- **Profile A** — the audible device→phone return channel (`spec/PROFILE-A.md`).
  Single-lane MFSK 2.3–4.1 kHz, `2·f_min > f_max` so no square-wave harmonic
  can land on a tone. `earshot_profile_t` and single-lane decode in the C
  receiver; `include/earshot_tx.h` + `src/tx/earshot_tx.c`, an integer-only
  device transmitter (no libm, no floating point) that drives one GPIO pin.
- `tools/soliton_table.py` generates `src/tx/soliton_table.h`, a fixed-point
  robust-soliton CDF verified bit-exact against the float decoder over all
  65536 seeds for every K.
- `tools/earshot_tx.py` gains square-wave synthesis, a Schroeder reverberator
  and duty-cycle-error modelling; `tests/emit` drives the device transmitter;
  the end-to-end bench covers Profile A under harmonics, reverb and noise.

### Changed (from the pre-release `ultra` prototype)
- Renamed project and identifier prefix `ultra` → `earshot`.
- Message envelope gains `hdr` (version + key id), a 32-bit `counter`, and an
  8-byte SipHash-2-4 `tag`. `len` moves from offset 0 to offset 5. Old envelope
  was `[len][data][crc16]`.
- Receiver gains authentication-tag verification and the §5.4 acceptance rule
  (monotonic counter, physical-presence gate, optional one-time lock).
- Receiver locks on the rising edge of the sync tone. The prototype locked on
  any over-threshold sub-block, which let a transient wedge the state machine
  at a wrong frame phase permanently; found by the new corruption test.
- Peeling decoder is now iterative (explicit FIFO) instead of recursive, so
  decode depth cannot overflow a small stack.
- All source comments and identifiers translated from Italian to English.
