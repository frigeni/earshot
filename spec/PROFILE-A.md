# Profile A — audible return channel (device → phone)

**Status: not designed yet.** This is the second half of the project (brief
point 7): the device transmits diagnostics (error code, serial number, firmware
version, running hours, last events) by driving the piezo buzzer it usually
already has, and the phone receives it with its microphone.

Everything above the physical layer is unchanged from `SPEC.md`: same message
envelope, same CRCs, same fountain code, same state machine. Only the frequency
plan and the waveform synthesis differ.

## To be specified

1. **Frequency plan** around a piezo buzzer's 2–4 kHz resonance, with an
   explicit constraint: no 3rd or 5th harmonic of any tone may land on another
   tone in the plan, because the buzzer emits a square wave, not a sine. The
   plan comes with the harmonic-collision calculations before implementation.
2. **Profile selection**: by sync tone frequency (see `SPEC.md` §6). Profile A
   gets its own sync frequency in the audible band.
3. **Symbol duration**: free to grow. Throughput does not matter here — fifty
   bytes in ten seconds is plenty — so the symbol can be long for robustness
   against reverberation, which is far worse at 2–4 kHz than in the
   near-ultrasonic band.
4. **Authentication**: open question. SipHash-2-4 is integer-only and cheap even
   on a small MCU, but outbound diagnostics are read-only telemetry; the CRC may
   be enough. To be decided.
5. **Integer fountain**: the device-side encoder must not use floating point.
   Diagnostic messages are small and bounded, so `K` is small and bounded, and
   the robust-soliton CDF can ship as a precomputed integer table
   (`src/tx/soliton_table.h`). The `K` range to support must be fixed.
6. **Test bench**: synthesise a square wave with harmonics instead of a pure
   sine, add wideband industrial-style noise, and measure where the decoder
   fails. Extend `tools/earshot_diag.py` and `tests/`.

## Device transmitter constraints (brief point 7)

A minimal C file that drives one GPIO pin with a timer. No dependencies, no
floating point. It must run on a small microcontroller and even when the device
is half broken.
