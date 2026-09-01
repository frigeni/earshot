# Contributing to Earshot

Thanks for your interest. This is a small project with a deliberately small
scope; the bar for changes to the wire protocol is high, the bar for tests,
docs and portability fixes is low.

## Ground rules

- **The spec is the source of truth.** Code and `spec/SPEC.md` must agree. A
  change to on-air behaviour is a spec change first, discussed in an issue
  before any PR.
- **Bit-exact interoperability.** The C receiver, the web page and the Python
  tools must produce and accept identical bytes. Any change touching the
  PRNG, the fountain code, the CRCs, the tone plan or the envelope must update
  `tests/vectors.json` and pass on all three implementations.
- **The receiver stays embedded-honest.** No dynamic allocation, no
  floating-point in paths that a small MCU runs hot, no libc beyond the
  freestanding subset. New dependencies are effectively not accepted.
- **Keep it boring.** Correctness and honest documentation over cleverness and
  performance.

## Before you open a PR

1. `cd tests && ./run.sh` passes (builds the C under gcc and clang with
   `-Wall -Wextra -Werror`, runs the unit vectors and the end-to-end bench).
2. New behaviour has a test. Bug fixes come with the failing case.
3. Commit messages explain *why*. Reference the spec section you touched.
4. By submitting a contribution you agree it is licensed under Apache-2.0
   (see `LICENSE` §5).

## Security issues

Do not open a public issue for a vulnerability in the authentication or
anti-replay design. Contact the maintainers privately first.
