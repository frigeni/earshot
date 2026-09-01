# Earshot

**An acoustic data channel for configuring embedded devices that have no
network, no display and no keyboard.**

A phone opens a web page and holds it near the board. The board receives its
configuration through an ordinary microphone — near-ultrasonic tones, no
pairing, no app, no radio to certify. One transmitter configures every device
within earshot at once.

> **Status: pre-release, under construction.** The channel works and is
> verified; the authentication layer is specified ([`spec/SPEC.md`](spec/SPEC.md))
> and being implemented. Do not deploy yet.

## How it works

The transmitter loops a short message forever, encoded with an **LT fountain
code**. It never knows whether anyone is listening. A receiver that powers on
halfway through still reconstructs the whole message — there is no "the part I
missed". Full details in [`spec/SPEC.md`](spec/SPEC.md).

- 48 kHz mono audio, 16.4–18.8 kHz tone plan
- ~8 useful bytes per second
- C receiver: no `malloc`, ~8 KB flash, ~5 KB static RAM, < 2 MMAC/s on a Cortex-M4
- authenticated payloads (SipHash-2-4) with a monotonic replay counter

## What it does not do

- **Not a bulk transport.** ~8 B/s — configuration and keys, not firmware images.
- **No return channel (v1).** No acknowledgements. A device-to-phone direction
  over a piezo buzzer is planned (Profile A).
- **No confidentiality by itself.** Payloads are authenticated, not encrypted.
  Encrypt the payload in the application if it carries secrets.
- **No security in an unmodified build.** The repository ships a demonstration
  key only. You must generate your own — see [Security](#security).

## Prior art

Data-over-sound is not new; see [ggwave](https://github.com/ggerganov/ggwave)
for a well-known modem-style implementation. Earshot's difference is **broadcast
with a fountain code**: no back-channel, no session, late joiners get the whole
message.

## Repository layout

| path | contents |
|------|----------|
| `spec/`     | protocol specification — the source of truth |
| `include/`  | `earshot.h`, the public C API |
| `src/`      | portable C receiver |
| `web/`      | self-contained transmitter + receiver web page |
| `tools/`    | waveform generator, key generator, single-header amalgamation |
| `tests/`    | unit vectors and end-to-end bench, run in CI |
| `examples/` | hardware integration (STM32) |

## Security

Earshot authenticates every payload with a pre-shared key (one per `keyid`).
This is a possession-of-key trust model, like a Wi-Fi passphrase: whoever holds
the key can configure any device that accepts that `keyid`.

**The repository contains no usable key.** Before anything real:

1. Generate an operator file: `python tools/keygen.py` → `operator.json`.
2. Install the same key in your device firmware.
3. Keep `operator.json` out of version control (it is in `.gitignore`).

Replay is bounded by a 32-bit monotonic counter carried in each message and
stored by the device; first provisioning and large counter jumps require a
physical-presence signal. See `spec/SPEC.md` §5.

## License

[Apache-2.0](LICENSE). See [`NOTICE`](NOTICE).
