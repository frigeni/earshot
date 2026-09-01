#!/bin/sh
# CI entry point: build with warnings as errors, run unit and end-to-end tests.
set -e

cd "$(dirname "$0")/.."
: "${CC:=cc}"

echo "== build ($CC) =="
make clean >/dev/null
make CC="$CC" WARN="-Wall -Wextra -Werror"

echo "== unit tests =="
./tests/test_unit

echo "== end-to-end tests =="
python3 tests/test_e2e.py

echo "== all green =="
