# Device-side transmitter (Profile A)

Not implemented yet. Profile A is not designed — see `../../spec/PROFILE-A.md`.

This directory will hold `earshot_tx.c` (fountain encoder + envelope builder +
square-wave GPIO driver) and `soliton_table.h` (a precomputed integer
robust-soliton CDF for a bounded K range). No floating point, no libm, no
dependencies beyond `../siphash.c` and `../crc.c`.
