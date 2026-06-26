# Sprint 03 — Probe, persistent state, timekeeping

**Dates:** 2026-08-13 (Thu) → 2026-08-26 (Wed)
**Capacity:** 1 developer, ~30 h effective
**Planned:** 15 tasks, ~24 h — day-length tasks moved to sprint 04 (they are decoder work; latitude resolved as a per-device decoder constant)

## Goal

Everything the solar policy stands on, built before the policy itself: the variant probe, state that survives a reset, and a real clock.

## Why this order

The solar policy needs an accurate `dt` to decay its EWMA, state that survives the join-failure reset, and — for the day-length normalisation — a wall clock. Building the policy first would mean building it on three foundations that do not exist.

Two things worth knowing before starting:

- **The RTC is already in use.** `ArduinoLowPower` wraps `RTCZero` internally, so `LowPower.deepSleep()` has always been crystal-backed and accurate. This sprint does not make sleeping better; it makes the clock *readable*. Both libraries then own the same peripheral — that seam is task 10.
- **The RTC does not survive `NVIC_SystemReset()`.** SAMD21 has no backup domain, so the join-failure path at `:404` resets it along with everything else. Task 06 stashes the epoch in `.noinit` around the reset.

## Working agreement

- **Commit in small batches**: one function + its test (if available) + its documentation, per commit. Several commits per task; never one commit per task.
- Every non-trivial change gets a dated dev-note in `docs/dev-notes/`.
- **No test devices.** Verification is compile-only plus host-side tests. On-device work is parked in sprint 06.

## Task index

| # | Task | Est | Item |
|---|---|---|---|
| 01 | I2C probe for the INA219 | 2 h | 4 |
| 02 | Probe misdetect safety and fallback | 1–2 h | 4 |
| 03 | Host tests probe and policy selection | 1–2 h | 4 |
| 04 | Define the noinit struct with magic and version | 2 h | 5 |
| 05 | Wire persistence into boot and cold-boot detection | 2 h | 5 |
| 06 | Save and restore the RTC epoch across reset | 2 h | 5 |
| 07 | Host tests noinit validity and versioning | 2 h | 5 |
| 08 | Dev-note persistent state | 1 h | 5 |
| 09 | Add an RTCZero instance for reads | 2 h | 6 |
| 10 | Document the ArduinoLowPower and RTCZero ownership seam | 1 h | 6 |
| 11 | Request DeviceTimeReq on the first uplink after join | 2 h | 6 |
| 12 | DeviceTimeReq callback and GPS to UTC conversion | 2 h | 6 |
| 13 | Host tests GPS to UTC conversion | 1–2 h | 6 |
| 14 | Clock-valid flag and degraded mode | 2 h | 6 |
| 18 | Dev-note RTC and timekeeping | 1 h | 6 |

Tasks 15–17 (day length, its tests, the latitude constant) **moved to sprint 04** as S04-19/20/21. The solar policy keys on the raw EWMA, not clarity, so no firmware path needs day length — it is decoder work, and latitude is a per-device decoder constant alongside `FIRMWARE_VERSION`. Firmware stays genuinely one-binary.

## Exit criteria

- The probe selects a policy and the misdetect case is both safe and alarmed (sprint 01 task 09).
- State survives `NVIC_SystemReset()`, including the RTC epoch, with a version guard.
- Wall clock acquired via `DeviceTimeReq`, and the no-clock path works and is visible.
- Day length computable; season still keyed to water temperature, unchanged.
