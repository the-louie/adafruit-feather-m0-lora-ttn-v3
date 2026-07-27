# `.noinit` did not survive a physical RST-button press — verify the real path

**Date:** 2026-07-27 ~19:01 UTC
**Scope:** Observation from gisebo-05 bench bring-up. No code change. Feeds item 5
(`.noinit`) / item 12 / sprint-06 S06-06.

## Observation

Pressing the physical **RST button** on gisebo-05 (verified via the diagnostic
frame `reset_cause: external`) produced **`cold_boot: true`, `boot_counter: 1`** —
i.e. `persistValid()` returned false, so the `.noinit` `PersistState` (season,
voltage band, interval index, uplink counter, harvest, sun EWMA, clock validity,
`rtcEpoch`) was **not** restored and was reinitialised to defaults.

Both consecutive boots (the flash's power-on reset, then the RST press) showed
`boot_counter: 1` — the counter never incremented across the reset, confirming the
struct did not survive.

## Interpretation

Most likely the mechanical button press briefly glitches the 3.3 V rail → partial
SRAM decay → the CRC-16 over the body correctly rejects it → cold boot. That is
`persist.h` **working as designed** (it refused to restore possibly-corrupt state —
exactly the partial-decay case the CRC exists for), not a bug.

**But it means the actual path is unverified.** `persist.h`'s premise is that
`.noinit` survives **`NVIC_SystemReset()`** — the clean soft reset the PROD
join-failure path performs, and on which the clock-preservation (stashing
`rtcEpoch` before the reset, S03-06) depends. A physical RST-pin press is a
*different, harder* reset than `NVIC_SystemReset()`, so this observation does not
tell us whether the real path preserves `.noinit`. It is a hint that reset-survival
must be checked deliberately, not assumed.

## What still needs verifying (see TODO item 17)

1. `.noinit` **survives `NVIC_SystemReset()`** — the path that matters. Trigger it
   controllably (force a join failure to hit the PROD 3-min timeout → 15-min sleep
   → `NVIC_SystemReset()`, or add a DEV test hook) and confirm `boot_counter`
   increments and season/interval/harvest carry over.
2. Characterise the RST-pin and watchdog resets too.
3. Confirm a brief power interruption yields a cold boot (the CRC catching a
   false-valid magic) — the standing S06-06 test.

## Impact if `.noinit` does not survive a given reset

Recoverable but real: season re-enters at Summer and takes ~2 uplinks to settle;
interval resets to the 5-min initial; the **harvest accumulator resets to 0** (a
visible backend discontinuity); the clock is re-requested via `DeviceTimeReq`; the
uplink counter resets. The harvest discontinuity and the clock-preservation
dependency are the ones worth confirming.
