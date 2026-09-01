# Earshot

**An acoustic data channel for embedded devices that have no network, no
display and no keyboard.**

Configuration goes *in* over near-ultrasonic tones: a phone opens a web page,
holds it near the board, and the board receives its config through an ordinary
microphone. Diagnostics come back *out* in the audible band: the device drives
its piezo buzzer and the phone listens. No pairing, no app, no radio to certify.
One transmitter reaches every device within earshot at once.

> **Status: v1 draft.** Both directions work and are covered by CI (gcc, clang,
> `arm-none-eabi`, sanitizers). The wire protocol (`ver = 1`) is not frozen and
> has had no external review. Generate your own key before deploying anything.

## How it works

The transmitter loops a short message forever, encoded with an **LT fountain
code**, without knowing whether anyone is listening. A receiver that starts
halfway through still reconstructs the whole message — there is no "the part I
missed". Full details in [`spec/SPEC.md`](spec/SPEC.md) and
[`spec/PROFILE-A.md`](spec/PROFILE-A.md).

|  | Profile N — config in | Profile A — diagnostics out |
|---|---|---|
| direction | phone → device | device → phone |
| band | 16.4–18.8 kHz, dual-tone | 2.3–4.1 kHz, single-tone MFSK |
| transmitter | web page (Web Audio) | one GPIO pin, square wave |
| receiver | C, this repo | web page (or the C decoder) |
| rate | ~8 useful B/s | ~5 B/s (speed does not matter here) |

Every payload is authenticated with **SipHash-2-4** over the same envelope.
Inbound config also carries a 32-bit monotonic **replay counter**; first
provisioning and large counter jumps need a physical-presence signal (a button,
or a 60 s power-on window).

## Measured footprint

`arm-none-eabi-gcc -Os`, from CI:

| | flash (`.text`) | RAM (state) |
|---|---|---|
| **receiver** (Profile N) | ~4.4 KB + libm | 5.8 KB (K≤32) · 6.75 KB (K≤64) · 12.5 KB (K≤255) |
| **device transmitter** (Profile A) | ~0.4 KB + soliton table + SipHash/CRC (~1.2 KB) | 280 B |

The transmitter's soliton table is 4.3 KB for K = 1..32; regenerate it for one
K (`tools/soliton_table.py --kmax <K>`) and it drops to a few hundred bytes.
Receiver CPU: the Goertzel bank is ~1.6 MMAC/s — a few percent of a Cortex-M4F.
No `malloc` anywhere; no floating point on the transmitter.

## What it does not do

- **Not a bulk transport.** A few bytes per second — configuration and keys, not
  firmware images.
- **No confidentiality by itself.** Payloads are authenticated, not encrypted.
  Encrypt in the application if the config carries secrets.
- **No anti-replay on diagnostics.** Outbound telemetry is signed but not
  counter-checked; the phone takes no action on it.
- **No security in an unmodified build.** The repo ships a demonstration key
  only — see [Security](#security).

## Prior art

Data-over-sound is not new; see [ggwave](https://github.com/ggerganov/ggwave)
for a well-known modem-style implementation. Earshot's difference is **broadcast
with a fountain code**: no back-channel, no session, late joiners get the whole
message.

## Repository layout

| path | contents |
|------|----------|
| `spec/`     | protocol specification — the source of truth |
| `include/`  | `earshot.h` (receiver API), `earshot_tx.h` (device transmitter API) |
| `src/`      | portable C receiver; `src/tx/` the integer-only device transmitter |
| `web/`      | self-contained transmitter + receiver web page |
| `tools/`    | waveform generator, key generator, soliton table, amalgamation |
| `tests/`    | unit vectors and end-to-end bench, run in CI |
| `examples/` | `stm32/` receiver integration, `buzzer/` transmitter integration |

## Build

```
make            # libearshot.a, the CLI decoder, unit tests, the device tx
make test       # unit tests + end-to-end bench (needs python3)
sh tests/run.sh # what CI runs: build with -Werror, then the tests
```

`tools/earshot_tx.py` synthesises a signed waveform and `tests/decode` recovers
it; `tests/emit` drives the device transmitter for Profile A.

## Security

Earshot authenticates every payload with a pre-shared key (one per `keyid`).
This is a possession-of-key trust model, like a Wi-Fi passphrase: whoever holds
the key can configure any device that accepts that `keyid`, and extracting the
key from any transmitter or device compromises that `keyid` everywhere. See
[`SECURITY.md`](SECURITY.md).

**The repository contains no usable key.** Before anything real:

1. `python3 tools/keygen.py` → `operator.json` (a fresh random key).
2. Install the same key in your device firmware.
3. Keep `operator.json` out of version control (it is in `.gitignore`).

## License

[Apache-2.0](LICENSE). See [`NOTICE`](NOTICE).
