#!/usr/bin/env python3
"""
keygen.py - create or update an Earshot operator file (SPEC 5.3).

  python3 keygen.py                 # write operator.json with a fresh key
  python3 keygen.py --keyid 3
  python3 keygen.py --bump          # increment the counter after signing

The operator file holds the pre-shared key and the message counter. Keep it out
of version control (it is in .gitignore) and install the same key in the device
firmware. An unmodified build ships only the insecure demonstration key.

SPDX-License-Identifier: Apache-2.0
"""
import argparse
import json
import os
import secrets
import sys


def _write(path, op):
    with open(path, "w") as fh:
        json.dump(op, fh, indent=2)
        fh.write("\n")
    try:
        os.chmod(path, 0o600)
    except OSError:
        pass


def main():
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("path", nargs="?", default="operator.json")
    ap.add_argument("--keyid", type=int, default=0, help="key id 0..15")
    ap.add_argument("--bump", action="store_true",
                    help="increment the counter in an existing file and exit")
    ap.add_argument("--force", action="store_true", help="overwrite an existing file")
    args = ap.parse_args()

    if args.bump:
        with open(args.path) as fh:
            op = json.load(fh)
        op["counter"] = int(op["counter"]) + 1
        _write(args.path, op)
        print("counter -> %d" % op["counter"])
        return

    if not 0 <= args.keyid <= 15:
        sys.exit("keyid must be 0..15")
    if os.path.exists(args.path) and not args.force:
        sys.exit("%s already exists (use --force to overwrite)" % args.path)

    op = {
        "version": 1,
        "keyid": args.keyid,
        "key": secrets.token_hex(16),
        "counter": 0,
    }
    _write(args.path, op)
    print("wrote %s (keyid %d)" % (args.path, args.keyid))
    print("install this key in the device firmware:")
    print("  " + op["key"])
    print("keep %s private - it is the only secret protecting your devices" % args.path)


if __name__ == "__main__":
    main()
