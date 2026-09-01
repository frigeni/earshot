# Earshot — Protocol Specification

**Version 1 (draft) — 2026-09-01**

> Status: this document is under review. The channel layers (§3, §4) describe the
> existing, verified prototype. The authentication layer (§5) is a new design,
> agreed but not yet implemented.

---

## 1. What Earshot is

Earshot is a one-way acoustic data channel for delivering small configuration
payloads to embedded devices that have no network, no display and no keyboard.
A phone or computer plays a modulated tone sequence through its speaker; the
device receives it through an ordinary microphone. No pairing, no app, no radio
to certify. One transmitter configures any number of devices within earshot at
the same time.

A companion return direction (device to phone, over an on-board piezo buzzer) is
planned and will be specified in a later revision. This document specifies the
inbound direction only (**Profile N**, near-ultrasonic).

### 1.1 What Earshot is not

- **Not a bulk transport.** About 8 useful bytes per second. It carries
  configuration and keys, not firmware images.
- **Not bidirectional (v1).** No acknowledgements, no retransmission requests.
- **Not confidential by itself.** The payload is authenticated, not encrypted.
  If the configuration contains secrets, the application must encrypt the
  payload before handing it to Earshot.
- **Not a ranging or presence system.** It works when the device can hear the
  transmitter, and not otherwise.

### 1.2 Prior art

Data-over-sound is not new; see **ggwave** for a well-known modem-style
implementation. Earshot's distinguishing choice is **broadcast with a fountain
code**: the transmitter loops the message forever without knowing whether anyone
is listening, and a receiver that starts in the middle still recovers the whole
message. There is no "the part I missed".

---

## 2. Layered model

```
  application config bytes
  └─ message envelope        §5   framing + counter + authentication tag
     └─ fountain code        §4   LT code, robust soliton
        └─ frame             §3   1 sync symbol + 12 data symbols = 12 bytes
           └─ symbol         §3   two simultaneous tones, 80 ms
              └─ PCM         §3   48 kHz mono
```

---

## 3. Physical and frame layer

### 3.1 Audio format

48000 samples per second, mono. The reference transmitter emits `int16` PCM;
the reference receiver accepts `int16` and works internally in normalised float.

### 3.2 Symbol

One symbol is 80 ms = 3840 samples, processed as 4 sub-blocks of 20 ms =
960 samples. A symbol carries two simultaneous sinusoids with a 6 ms
raised-cosine ramp at each end.

### 3.3 Tone plan (Profile N)

| purpose               | frequency                     | count                       |
|-----------------------|-------------------------------|-----------------------------|
| sync                  | 16400 Hz                      | 1                           |
| lane A (low nibble)   | 16700 + 60·i Hz, i = 0..15     | 16 → 16700..17600 Hz        |
| lane B (high nibble)  | 17900 + 60·i Hz, i = 0..15     | 16 → 17900..18800 Hz        |

A data symbol is one lane-A tone and one lane-B tone played together, encoding
one byte: `byte = A_index | (B_index << 4)`.

### 3.4 Frame

A frame is 13 symbols = 1040 ms:

- **symbol 0: sync** (the sync tone alone). Present only in the first symbol of
  the frame; it marks frame start and, by its frequency, the profile (§6).
- **symbols 1..12: 12 data symbols = 12 bytes**, laid out as:

| byte  | meaning                                                        |
|-------|----------------------------------------------------------------|
| 0     | `K` — number of source blocks in the message, 1..255          |
| 1     | `seed` low byte                                               |
| 2     | `seed` high byte                                              |
| 3     | `crc8` of bytes 0,1,2 followed by bytes 4..11                 |
| 4..11 | `payload` — 8 bytes, one fountain-coded block (§4)            |

`crc8`: polynomial 0x07, MSB-first, initial value 0x00, no final XOR.

Raw frame rate ≈ 11.5 byte/s; useful message rate after fountain overhead
≈ 8 byte/s.

### 3.5 Receiver notes (informative)

The reference receiver runs a 33-bin Goertzel (1 sync + 16 lane A + 16 lane B)
on each 20 ms sub-block, with no dynamic allocation, under 2 MMAC/s on a
Cortex-M4. It locks on the **rising edge** of the sync tone — a sub-block at
least `SYNC_DB` above the in-band noise floor whose predecessor was not — rather
than on any sub-block over threshold; without this a transient can lock the
state machine at a wrong phase that then repeats every frame and never
recovers. It then samples the two middle sub-blocks of each following symbol;
per lane it takes the strongest bin provided it clears the floor by `TONE_DB`
and the runner-up by `MARGIN_DB`, otherwise the frame is marked bad and dropped.

---

## 4. Fountain code layer

The message envelope (§5) is split into `K` source blocks of 8 bytes, the last
zero-padded. Each transmitted frame carries one coded block: the bytewise XOR of
a pseudo-random subset of the `K` source blocks. The subset is derived entirely
from the 16-bit `seed` in the frame, so the receiver reconstructs the membership
from the seed alone.

### 4.1 PRNG

`xorshift32`, 32-bit state `s`. Each draw advances the state

```
s ^= s << 13
s ^= s >> 17
s ^= s << 5
```

and returns the new state divided by 2³² as a value in [0, 1). A zero seed is
replaced by `0x9E3779B9`.

### 4.2 Degree distribution

Robust soliton with `c = 0.05`, `delta = 0.05`.

```
R       = c · ln(K / delta) · sqrt(K)
rho(1)  = 1 / K
rho(d)  = 1 / (d·(d-1))                     for d = 2..K
kr      = round(K / R)                       (at least 1)
tau(d)  = R / (d·K)                          for d = 1..kr-1
tau(kr) = R · ln(R / delta) / K
tau(d)  = 0                                  for d > kr
mu(d)   = (rho(d) + tau(d)) / Σ(rho + tau)
```

The CDF of `mu` is precomputed once per `K`.

### 4.3 Block selection from a seed

```
seed the PRNG with `seed`
d = smallest degree whose CDF value ≥ first PRNG draw     (clamped to K)
repeat: i = floor(next PRNG draw · K) mod K
        until d distinct indices have been collected
```

Those `d` indices are the source blocks XORed into this frame's payload.

### 4.4 Decoding

Standard peeling decoder with back-substitution: a frame whose block set reduces
to a single unknown resolves it directly; resolving a block simplifies every
pending frame that referenced it, which may cascade. The reference receiver
keeps up to `HOLD_MAX = 48` unresolved frames. The message is complete when all
`K` blocks are known.

Because the transmitter loops, a receiver may start at any point and still
collect enough independent frames. Repeated seeds (the transmitter repeats its
seed schedule every cycle) contribute nothing but do no harm.

---

## 5. Message envelope and authentication

### 5.1 Layout

All multi-byte integers are big-endian.

```
offset   size   field
0        1      hdr       = (ver << 4) | keyid
1        4      counter   monotonic, per keyid (§5.4)
5        2      len       length of `data`, 1..2023
7        len    data      application configuration bytes
7+len    8      tag       SipHash-2-4(key, signed region)
15+len   2      crc16     CRC-16/CCITT-FALSE over offsets 0 .. 14+len
```

- `ver` (4 bits): protocol version. This document is `ver = 1`. A receiver
  rejects any other value.
- `keyid` (4 bits): selects which pre-shared key the tag was computed with, and
  which counter the receiver compares against. Up to 16 concurrent keys.
- **signed region**: offsets `0 .. 6+len` inclusive (`hdr`, `counter`, `len`,
  `data`). The tag covers all of it.
- `crc16` is a non-cryptographic integrity check only (init 0xFFFF, poly 0x1021,
  no reflection, no final XOR). It lets the receiver discard a mis-reconstructed
  envelope before spending cycles on SipHash.

Total envelope size = `len + 17` bytes, so `K = ceil((len + 17) / 8)`.

### 5.2 Authentication tag

The tag is the full 64-bit output of **SipHash-2-4** (Aumasson–Bernstein) over
the signed-region bytes in wire order, using the 128-bit pre-shared key
serialised little-endian as in the SipHash reference. No truncation: 64 bits is
the native output.

Rationale for 64 bits: forging a tag without the key succeeds with probability
2⁻⁶⁴ per attempt, and one attempt costs 25–40 s of audio played at the device
with no feedback channel. SipHash-2-4 is about 200 bytes of code, needs no
tables and no field arithmetic, and fits the receiver's constraints well.

The receiver MUST compare tags in constant time.

### 5.3 Key management

Earshot uses a single pre-shared key per `keyid`. The transmitter holds the key;
there is no backend and no online step. This is a possession-of-key trust model,
like a Wi-Fi passphrase: anyone who has the key can configure any device that
accepts that `keyid`, and extracting the key from any transmitter or any device
compromises that `keyid` everywhere.

**The public repository ships no usable key.** It contains only a demonstration
key, clearly marked insecure. An integrator MUST generate their own key and
install it in both the device firmware and their private copy of the
transmitter. An unmodified build has no security.

The transmitter stores its key and counter in an operator file:

```json
{
  "version": 1,
  "keyid": 3,
  "key": "0123456789abcdef0123456789abcdef",
  "counter": 42
}
```

`key` is 16 bytes as 32 hex characters. `counter` is the last counter value
used (0 in a fresh file). After each signing the transmitter rewrites the file
with the new value and offers it for download; `localStorage` is only a
within-session convenience.

### 5.4 Anti-replay

**Counter.** The transmitter maintains a 32-bit counter, incremented by one for
every signed message, carried in the signed region. It is not a timestamp: a
wall-clock source was rejected because a wrong clock on the signing device fails
silently and per-device.

**Receiver state.** For each `keyid` it accepts, the device stores in
non-volatile memory the highest counter value it has accepted (0 if none).
Writes use two slots with a sequence tag, so a power loss mid-write cannot
corrupt the stored value.

**Acceptance rule.** After the envelope is reconstructed and `crc16`, `tag` and
`ver == 1` all verify and `keyid` is known, let `stored` be the saved counter
for that `keyid`:

1. If the one-time lock is enabled and the device has already accepted a message
   since manufacture → **reject**.
2. If `counter <= stored` → **reject** (replay or downgrade). No exception;
   recovery from an operator counter mistake is a physical reset (§5.5).
3. If `stored == 0` (never provisioned) → accept **only if a physical-presence
   signal is active** (§5.5).
4. If `counter - stored > Δ` (`Δ = 1024`) → accept **only if the button was
   pressed** (§5.5); the power-on window alone is not sufficient here.
5. Otherwise → **accept** with no physical-presence requirement.

On acceptance the device stores `counter` as the new `stored` for that `keyid`,
sets its provisioned flag, and applies `data`.

Case 4 (a large forward jump on a live device) essentially only arises from
operator error in a counter-resume value, or from key compromise; requiring the
button confines the damage.

### 5.5 Physical presence and reset

A **physical-presence signal** is active when either:

- the device is within 60 s of power-on (the **provisioning window**), measured
  on the 48 kHz sample clock; or
- the device has a button and it was pressed within the last 60 s.

A device that declares no button relies on the provisioning window alone, and so
cannot satisfy case 4. It also has a narrow first-provisioning replay exposure:
an attacker who captured the very first legitimate message could replay it
inside a power-on window before the operator provisions the device. A button
removes both.

The device MUST provide a **reset mechanism** — button held during boot, a
jumper, or re-flashing — that clears all stored counters and the provisioned
flag. After a reset the device is in the `stored == 0` state for every `keyid`.

The **one-time lock** is an integrator build option. When enabled, the device
accepts exactly one message in its lifetime (subject to all other rules) and
then ignores the channel until a physical reset.

---

## 6. Profiles

A profile fixes the frequency plan, tone count and symbol duration. The profile
in use is identified by **the sync tone frequency**: a receiver scans for the
sync tones of the profiles it supports and thereby knows which plan to decode.
The frame has no spare bits for a profile field.

- **Profile N** (this document): near-ultrasonic 16.4–18.8 kHz, inbound
  (phone → device).
- **Profile A**: audible, single-lane MFSK around a piezo buzzer's 2–4 kHz
  resonance, for the outbound direction (device → phone). Specified in
  `PROFILE-A.md`.

---

## 7. Default parameters

| name                  | value                                          | ref   |
|-----------------------|------------------------------------------------|-------|
| sample rate           | 48000 Hz                                        | §3.1  |
| symbol length         | 80 ms / 3840 samples                            | §3.2  |
| sub-block             | 20 ms / 960 samples                             | §3.2  |
| frame                 | 13 symbols / 1040 ms / 12 bytes                 | §3.4  |
| sync tone (Profile N) | 16400 Hz                                        | §3.3  |
| lane A                | 16700 Hz + 60 Hz·i, i = 0..15                   | §3.3  |
| lane B                | 17900 Hz + 60 Hz·i, i = 0..15                   | §3.3  |
| crc8                  | poly 0x07, init 0x00                            | §3.4  |
| crc16                 | CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF)   | §5.1  |
| fountain              | robust soliton, c = 0.05, delta = 0.05          | §4.2  |
| PRNG                  | xorshift32 (13, 17, 5), zero → 0x9E3779B9       | §4.1  |
| MAC                   | SipHash-2-4, 128-bit key, 64-bit tag            | §5.2  |
| protocol version      | 1                                              | §5.1  |
| forward-jump gate Δ   | 1024                                           | §5.4  |
| provisioning window   | 60 s                                           | §5.5  |
| button latch          | 60 s                                           | §5.5  |
| max payload           | 2023 bytes                                      | §5.1  |

---

## 8. Test vectors

To be filled in during implementation. At minimum:

- SipHash-2-4: key, message, expected tag (cross-checked against the published
  SipHash reference vectors).
- crc8 / crc16 over a fixed buffer.
- xorshift32: seed and first 8 draws.
- robust soliton CDF for `K = 19`.
- one full end-to-end vector: config bytes → envelope → `K` and seed schedule →
  PCM (hash) → decoded config.

---

## 9. Changes from the pre-release prototype

- The message envelope gains `hdr`, `counter` and `tag`; `len` moves from
  offset 0 to offset 5. The old envelope was `[len][data][crc16]`.
- The receiver gains tag verification and the §5.4 acceptance rule.
- The project and the identifier prefix are renamed from `ultra` to `earshot`.
