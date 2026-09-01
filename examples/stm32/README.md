# STM32 integration example

> Placeholder — to be written (brief point 5).

Planned contents:

- **Microphone**: a MEMS part whose response actually reaches 20 kHz. Many cheap
  MEMS mics have an anti-alias filter tuned for voice that rolls off around
  16 kHz and will not hear Profile N. Part numbers and response curves go here.
- **Acquisition**: I2S or PDM microphone into DMA at 48 kHz, double-buffered,
  feeding `earshot_feed()` from the half/complete-transfer interrupt.
- **Budget**: measured flash, static RAM and CPU load on a representative
  Cortex-M0+ and M4, with and without an FPU.
- **NVM**: a two-slot counter store in internal flash or emulated EEPROM,
  wired to the `counter_load` / `counter_store` hooks.
- **Presence**: wiring the provisioning button to `button_recent`, and the
  reset (button-at-boot) that clears the counter store.
- Full buildable project (CubeMX or bare register setup, TBD).
