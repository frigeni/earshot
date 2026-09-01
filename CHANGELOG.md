# Changelog

All notable changes to Earshot are recorded here. The protocol version is the
`ver` field in the message envelope (`spec/SPEC.md` §5.1).

## [Unreleased]

### Added
- Protocol specification `spec/SPEC.md` (draft, protocol `ver = 1`).
- Repository structure: portable C receiver, web transmitter, Python tools,
  CI-run tests, STM32 example.

### Changed (from the pre-release `ultra` prototype)
- Renamed project and identifier prefix `ultra` → `earshot`.
- Message envelope gains `hdr` (version + key id), a 32-bit `counter`, and an
  8-byte SipHash-2-4 `tag`. `len` moves from offset 0 to offset 5. Old envelope
  was `[len][data][crc16]`.
- Receiver gains authentication-tag verification and the §5.4 acceptance rule
  (monotonic counter, physical-presence gate, optional one-time lock).
- Receiver locks on the rising edge of the sync tone. The prototype locked on
  any over-threshold sub-block, which let a transient wedge the state machine
  at a wrong frame phase permanently; found by the new corruption test.
- Peeling decoder is now iterative (explicit FIFO) instead of recursive, so
  decode depth cannot overflow a small stack.
- All source comments and identifiers translated from Italian to English.
