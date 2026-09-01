#!/usr/bin/env python3
"""
earshot_tx.py - reference Profile N transmitter, for tests and experiments.

Builds a signed message envelope (SPEC 5), fountain-codes it (SPEC 4), and
synthesises the 48 kHz mono waveform (SPEC 3). Output is raw signed 16-bit
little-endian PCM on stdout, or to --out.

  python3 earshot_tx.py --text "hello" --operator operator.json > out.pcm
  python3 earshot_tx.py --hex 0011..  --key 00112233445566778899aabbccddeeff \\
                        --keyid 0 --counter 1 --noise 0.02 --skip 40 > out.pcm

This file is also imported by the test bench for its codec functions.

SPDX-License-Identifier: Apache-2.0
"""
import argparse
import array
import json
import math
import random
import struct
import sys

SR = 48000
SYM = 0.080
SYNC = 16400.0
LANE_A = 16700.0
LANE_B = 17900.0
STEP = 60.0
BLOCK = 8

# Demonstration key only. NOT SECRET. An unmodified build has no security.
DEMO_KEY = bytes.fromhex("00112233445566778899aabbccddeeff")

MASK64 = (1 << 64) - 1


# ---- SipHash-2-4 (SPEC 5.2) ------------------------------------------------

def _rotl(x, b):
    return ((x << b) | (x >> (64 - b))) & MASK64


def siphash24(key, msg):
    assert len(key) == 16
    k0 = int.from_bytes(key[:8], "little")
    k1 = int.from_bytes(key[8:], "little")
    v0 = 0x736F6D6570736575 ^ k0
    v1 = 0x646F72616E646F6D ^ k1
    v2 = 0x6C7967656E657261 ^ k0
    v3 = 0x7465646279746573 ^ k1

    def rounds(n):
        nonlocal v0, v1, v2, v3
        for _ in range(n):
            v0 = (v0 + v1) & MASK64; v1 = _rotl(v1, 13); v1 ^= v0; v0 = _rotl(v0, 32)
            v2 = (v2 + v3) & MASK64; v3 = _rotl(v3, 16); v3 ^= v2
            v0 = (v0 + v3) & MASK64; v3 = _rotl(v3, 21); v3 ^= v0
            v2 = (v2 + v1) & MASK64; v1 = _rotl(v1, 17); v1 ^= v2; v2 = _rotl(v2, 32)

    b = (len(msg) & 0xFF) << 56
    whole = len(msg) - (len(msg) % 8)
    for i in range(0, whole, 8):
        m = int.from_bytes(msg[i:i + 8], "little")
        v3 ^= m; rounds(2); v0 ^= m
    for i in range(whole, len(msg)):
        b |= msg[i] << (8 * (i - whole))
    v3 ^= b; rounds(2); v0 ^= b
    v2 ^= 0xFF; rounds(4)
    return (v0 ^ v1 ^ v2 ^ v3) & MASK64


# ---- CRCs (SPEC 3.4, 5.1) -------------------------------------------------

def crc8(b):
    c = 0
    for x in b:
        c ^= x
        for _ in range(8):
            c = ((c << 1) ^ 0x07) & 0xFF if c & 0x80 else (c << 1) & 0xFF
    return c


def crc16(b):
    c = 0xFFFF
    for x in b:
        c ^= x << 8
        for _ in range(8):
            c = ((c << 1) ^ 0x1021) & 0xFFFF if c & 0x8000 else (c << 1) & 0xFFFF
    return c


# ---- fountain code (SPEC 4) ---------------------------------------------

def prng(seed):
    s = (seed & 0xFFFFFFFF) or 0x9E3779B9

    def nxt():
        nonlocal s
        s ^= (s << 13) & 0xFFFFFFFF
        s ^= s >> 17
        s ^= (s << 5) & 0xFFFFFFFF
        return s / 4294967296.0

    return nxt


def soliton_cdf(K):
    c = d = 0.05
    R = c * math.log(K / d) * math.sqrt(K)
    p = [0.0] * (K + 2)
    for i in range(1, K + 1):
        p[i] = 1.0 / K if i == 1 else 1.0 / (i * (i - 1))
    kr = max(1, int(math.floor(K / R + 0.5)))
    for i in range(1, min(kr, K + 1)):
        p[i] += R / (i * K)
    if kr <= K:
        p[kr] += R * math.log(R / d) / K
    tot = sum(p[1:K + 1])
    cdf = [0.0] * (K + 1)
    acc = 0.0
    for i in range(1, K + 1):
        acc += p[i] / tot
        cdf[i] = acc
    cdf[K] = 1.0
    return cdf


def _degree(cdf, K, r):
    lo, hi = 1, K
    while lo < hi:
        m = (lo + hi) // 2
        if cdf[m] < r:
            lo = m + 1
        else:
            hi = m
    return lo


def block_indices(seed, K, cdf):
    r = prng(seed)
    d = min(K, _degree(cdf, K, r()))
    s = set()
    while len(s) < d:
        s.add(int(r() * K) % K)
    return sorted(s)


# ---- envelope (SPEC 5.1) -----------------------------------------------

def build_envelope(data, key, keyid, counter, ver=1):
    if not 1 <= len(data) <= 255 * BLOCK - 17:
        raise ValueError("payload length out of range")
    hdr = ((ver & 0xF) << 4) | (keyid & 0xF)
    signed = bytes([hdr]) + struct.pack(">I", counter) + struct.pack(">H", len(data)) + data
    tag = siphash24(key, signed).to_bytes(8, "little")
    body = signed + tag
    return body + struct.pack(">H", crc16(body))


def fountain_frames(env, count, seed_fn=None):
    if seed_fn is None:
        seed_fn = lambda n: (n * 40503) & 0xFFFF or 1
    K = (len(env) + BLOCK - 1) // BLOCK
    if K > 255:
        raise ValueError("message too long: K=%d > 255" % K)
    cdf = soliton_cdf(K)
    src = [env[i * BLOCK:(i + 1) * BLOCK].ljust(BLOCK, b"\0") for i in range(K)]
    frames = []
    for n in range(1, count + 1):
        seed = seed_fn(n)
        p = bytearray(BLOCK)
        for i in block_indices(seed, K, cdf):
            for j in range(BLOCK):
                p[j] ^= src[i][j]
        f = bytearray(4 + BLOCK)
        f[0] = K
        f[1] = seed & 0xFF
        f[2] = seed >> 8
        f[4:] = p
        f[3] = crc8(bytes([f[0], f[1], f[2]]) + bytes(p))
        frames.append(bytes(f))
    return K, frames


# ---- waveform (SPEC 3) -------------------------------------------------

_SYM_CACHE = {}


def _symbol(freqs, amp):
    key = (amp, tuple(freqs))
    hit = _SYM_CACHE.get(key)
    if hit is not None:
        return hit
    n = int(SR * SYM)
    ramp = int(SR * 0.006)
    y = [0.0] * n
    for f in freqs:
        w = 2 * math.pi * f / SR
        for i in range(n):
            g = amp
            if i < ramp:
                g *= i / ramp
            elif i > n - ramp:
                g *= (n - i) / ramp
            y[i] += g * math.sin(w * i)
    _SYM_CACHE[key] = y
    return y


def synth(frames, noise=0.0, offset=0):
    y = [0.0] * offset
    for f in frames:
        y += _symbol((SYNC,), 0.5)
        for byte in f:
            y += _symbol((LANE_A + (byte & 15) * STEP, LANE_B + (byte >> 4) * STEP), 0.25)
    if noise:
        y = [v + random.gauss(0, noise) for v in y]
    return y


def pcm_bytes(samples):
    a = array.array(
        "h", (max(-32767, min(32767, int(v * 20000))) for v in samples))
    if sys.byteorder != "little":
        a.byteswap()
    return a.tobytes()


# ---- CLI --------------------------------------------------------------

def _load_key(args):
    if args.operator:
        with open(args.operator) as fh:
            op = json.load(fh)
        key = bytes.fromhex(op["key"])
        keyid = op.get("keyid", 0)
        counter = op["counter"] + 1
    else:
        key = bytes.fromhex(args.key) if args.key else DEMO_KEY
        keyid = args.keyid
        counter = 1
    if args.keyid is not None:
        keyid = args.keyid
    if args.counter is not None:
        counter = args.counter
    if key == DEMO_KEY:
        sys.stderr.write("WARNING: using the demonstration key. This has no security.\n")
    return key, keyid, counter


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    src = ap.add_mutually_exclusive_group()
    src.add_argument("--text", help="UTF-8 message")
    src.add_argument("--file", help="read the payload from a file")
    src.add_argument("--hex", help="payload as hex")
    ap.add_argument("--key", help="16-byte key as 32 hex chars")
    ap.add_argument("--operator", help="operator.json with key/keyid/counter")
    ap.add_argument("--keyid", type=int, default=None)
    ap.add_argument("--counter", type=int, default=None)
    ap.add_argument("--frames", type=int, default=240, help="frames to emit")
    ap.add_argument("--noise", type=float, default=0.0, help="Gaussian noise sigma")
    ap.add_argument("--skip", type=int, default=0, help="drop the first N frames")
    ap.add_argument("--loss", type=float, default=0.0, help="drop each frame with prob P")
    ap.add_argument("--offset", type=int, default=1234, help="leading silence, samples")
    ap.add_argument("--seed", type=int, default=7, help="RNG seed for noise/loss")
    ap.add_argument("--out", help="output file (default: stdout)")
    args = ap.parse_args()

    if args.text is not None:
        data = args.text.encode("utf-8")
    elif args.file:
        with open(args.file, "rb") as fh:
            data = fh.read()
    elif args.hex:
        data = bytes.fromhex(args.hex)
    else:
        data = b"If you can read this, it arrived as sound, with no network in between."

    key, keyid, counter = _load_key(args)
    env = build_envelope(data, key, keyid, counter)
    K, frames = fountain_frames(env, args.frames)

    random.seed(args.seed)
    kept = [f for i, f in enumerate(frames)
            if i >= args.skip and random.random() >= args.loss]
    audio = synth(kept, noise=args.noise, offset=args.offset)

    sys.stderr.write(
        "payload %d B - envelope %d B - K=%d - frames emitted %d - "
        "keyid %d - counter %d - noise %.4f\n"
        % (len(data), len(env), K, len(kept), keyid, counter, args.noise))

    blob = pcm_bytes(audio)
    if args.out:
        with open(args.out, "wb") as fh:
            fh.write(blob)
    else:
        sys.stdout.buffer.write(blob)


if __name__ == "__main__":
    main()
