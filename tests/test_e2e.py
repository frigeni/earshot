#!/usr/bin/env python3
"""
test_e2e.py - end-to-end bench (SPEC 8).

Synthesises the waveform with tools/earshot_tx.py, decodes it with the compiled
`tests/decode`, and checks the recovered payload. Covers clean audio, additive
noise, a late-joining receiver, frame loss, mid-stream corruption, and the
authentication rejections.

Also cross-checks the Python primitives against the published constants, so a
divergence between the Python and C implementations is caught here.

SPDX-License-Identifier: Apache-2.0
"""
import os
import random
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools"))
import earshot_tx as tx  # noqa: E402

DECODE = os.path.join(HERE, "decode")
KEY = bytes(range(16))
KEYHEX = KEY.hex()
ALT_KEYHEX = bytes(range(1, 17)).hex()

PAYLOAD = ("If you can read this, it arrived as sound. The C receiver "
           "reassembled it from frames out of order.").encode("utf-8")

fails = 0


def note(ok, name, extra=""):
    global fails
    print(("ok   " if ok else "FAIL ") + name + (("  " + extra) if extra else ""))
    if not ok:
        fails += 1


def frames_for(payload, count, counter=1, keyid=0):
    env = tx.build_envelope(payload, KEY, keyid, counter)
    return tx.fountain_frames(env, count)


def decode(pcm, *args, expect_rc=0):
    p = subprocess.run([DECODE, "--key", KEYHEX, *args],
                       input=pcm, capture_output=True)
    return p.returncode, p.stdout, p.stderr


def case(name, *, count=70, noise=0.0, skip=0, loss=0.0, corrupt=False,
         seed=7, counter=1, offset=1234, decode_args=(), expect_rc=0,
         expect_payload=True):
    K, allframes = frames_for(PAYLOAD, count, counter=counter)
    rng = random.Random(seed)
    kept = [f for i, f in enumerate(allframes)
            if i >= skip and rng.random() >= loss]
    audio = tx.synth(kept, noise=noise, offset=offset)
    if corrupt:
        # wipe a stretch of samples in the first third of the stream
        a = len(audio) // 5
        for i in range(a, a + 4000):
            audio[i] = 0.0
    pcm = tx.pcm_bytes(audio)
    rc, out, err = decode(pcm, *decode_args)
    ok = rc == expect_rc
    if ok and expect_rc == 0 and expect_payload:
        ok = out == PAYLOAD
    note(ok, name, "rc=%d K=%d kept=%d" % (rc, K, len(kept)))
    if not ok:
        sys.stderr.write(err.decode("utf-8", "replace"))


def primitives():
    ok = (tx.siphash24(KEY, b"") == 0x726FDB47DD0E0E31
          and tx.siphash24(KEY, bytes(range(1))) == 0x74F839C593DC67FD
          and tx.siphash24(KEY, bytes(range(15))) == 0xA129CA6149BE45E5)
    note(ok, "python siphash-2-4 vs reference vectors")

    ok = tx.crc8(b"123456789") == 0xF4 and tx.crc16(b"123456789") == 0x29B1
    note(ok, "python crc8 / crc16 vs check string")


def main():
    if not os.path.exists(DECODE):
        sys.exit("build tests/decode first (make)")

    primitives()

    case("clean signal")
    case("additive noise, mild", noise=0.010, count=90)
    case("late receiver (skip 55 frames)", skip=55, count=130)
    case("35% frame loss", loss=0.35, count=110)
    case("mid-stream corruption", corrupt=True, count=90)
    case("noise + loss + late", noise=0.008, loss=0.20, skip=20, count=130)

    # authentication / anti-replay
    case("replay: stored counter ahead", counter=1,
         decode_args=("--counter", "5"), expect_rc=3, expect_payload=False)
    case("wrong key: tag mismatch", counter=1,
         decode_args=("--key", ALT_KEYHEX, "--counter", "0"),
         expect_rc=3, expect_payload=False)
    case("forward jump > delta needs button", counter=3000,
         decode_args=("--counter", "1", "--no-button"),
         expect_rc=3, expect_payload=False)
    case("forward jump > delta with button", counter=3000,
         decode_args=("--counter", "1"), expect_rc=0)
    # first provisioning past the power-on window, no button -> needs presence
    case("first config after the provisioning window", counter=1, count=55,
         offset=2_900_000, decode_args=("--counter", "0", "--no-button"),
         expect_rc=3, expect_payload=False)

    print()
    if fails:
        print("%d failure(s)" % fails)
        sys.exit(1)
    print("end-to-end: all green")


if __name__ == "__main__":
    main()
