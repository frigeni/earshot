# Security policy

## Reporting

For a vulnerability in the authentication or anti-replay design, or in an
implementation, contact the maintainers privately before opening a public
issue. A public advisory follows once a fix is available.

## Threat model (v1)

Earshot authenticates every payload with a pre-shared key, one per `keyid`
(`spec/SPEC.md` §5.2–5.3). What this does and does not cover:

**Covered**
- Forgery without the key: a 64-bit SipHash-2-4 tag, ~2⁻⁶⁴ per attempt, and an
  attempt is 25–40 s of audio with no feedback channel.
- Replay and downgrade: a 32-bit monotonic counter carried in each message and
  stored per `keyid` in device NVM; `counter <= stored` is always rejected.
- Unattended hijack right after power-on: first provisioning and large counter
  jumps require a physical-presence signal (button, or a 60 s power-on window).

**Not covered**
- Key extraction. The key sits in the transmitter (a web page) and in device
  firmware. Reading it from either compromises that `keyid` everywhere. Use
  per-`keyid` scoping to limit blast radius.
- Confidentiality. Payloads are authenticated, not encrypted. Encrypt in the
  application if the configuration carries secrets.
- Denial of service. Anyone can jam the band or play noise; the channel simply
  stops working while they do.
- A no-button device has a narrow first-provisioning replay window (SPEC §5.5).

## Do not ship the demo key

The repository contains a demonstration key only. An unmodified build has no
security. Generate your own with `tools/keygen.py` and install the same key in
your firmware.
