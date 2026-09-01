# Piezo buzzer transmitter example (Profile A)

> Placeholder — waiting on the Profile A design (`spec/PROFILE-A.md`, brief
> point 7).

Planned contents: a minimal bare-metal example that drives one GPIO pin from a
hardware timer to emit the Profile A tone plan as a square wave — no HAL, no
floating point — plus the wiring to a typical piezo element and a note on
running it from a watchdog/fault handler so a half-broken device can still
report why it died.
