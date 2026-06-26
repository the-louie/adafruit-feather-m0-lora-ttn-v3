# Sprint 02 — Fix the confirmed bugs, split the core

**Dates:** 2026-07-30 (Thu) → 2026-08-12 (Wed)
**Capacity:** 1 developer, ~30 h effective
**Planned:** 20 tasks, ~33 h — **over capacity by ~3 h**; S02-06 (decoder work, not firmware) is the first candidate to push to sprint 03 if it bites

## Goal

Fix the two defects confirmed in sprint 01, then split the monolithic sketch behind a `PowerPolicy` interface. The refactor is deliberately **not** a behaviour change: `PrimaryCellPolicy` must be identical to today's algorithm, which sprint 01 confirmed works correctly in production (gisebo-01 sits at interval index 4 = Summer base + healthy battery, exactly as designed).

## Why this order

The bug fixes land first because they are small, confirmed, and independent — and because the refactor would otherwise carry two known defects into a new structure and make the diff unreadable.

Task 08 is the load-bearing one: a host-side 40-wake simulation that asserts every uplink carries a full batch. **The current code fails that test**, which is exactly why it is worth writing. With no hardware, host tests are the only executable verification that exists.

## Working agreement

- **Commit in small batches**: one function + its test (if available) + its documentation, per commit. Several commits per task; never one commit per task.
- Every non-trivial change gets a dated dev-note in `docs/dev-notes/`.
- **No test devices.** Every firmware task here is verified by compilation and host-side tests only. On-device verification is parked in sprint 06 and is a known, accepted risk.

## Task index

| # | Task | Est | Item |
|---|---|---|---|
| 01 | Choose and implement the idle 750 fix | 2 h | 1 |
| 02 | Compile and flash-size verification of the fix | 1 h | 1 |
| 03 | Dev-note the idle 750 fix | 1 h | 1 |
| 04 | Add firstUplinkAfterJoin and decouple the fast-flush | 2 h | 2 |
| 05 | Change the 4-bit field to an uplink counter | 1 h | 2 |
| 06 | Decoder uplink_counter and remove rebootDetected | 2 h | 2 |
| 07 | Host test harness for firmware logic | 2 h | 4 |
| 08 | Host test 40-wake simulation regression | 2 h | 2 |
| 09 | Dev-note the counter fix | 1 h | 2 |
| 10 | Extract the season state machine | 2 h | 4 |
| 11 | Host tests season machine and hysteresis | 2 h | 4 |
| 12 | Define the PowerPolicy interface | 2 h | 4 |
| 13 | Implement PrimaryCellPolicy | 2 h | 4 |
| 14 | Host tests primary interval ladder | 2 h | 4 |
| 15 | Extract payload assembly | 2 h | 4 |
| 16 | Wire the sketch to PowerPolicy | 2 h | 4 |
| 17 | lmic_project_config documentation and DeviceTimeReq flag | 1 h | 6 |
| 18 | Dev-note the refactor | 1 h | 4 |
| 19 | VBAT ADC averaging and primary voltage-band hysteresis | 2 h | 1, 8 |
| 20 | Host tests voltage hysteresis under noise | 2 h | 8 |

## The mitigation that got added late

The voltage bands are **bare thresholds** and will dither: ~±19 mV of single-sample ADC noise against a pack sitting on a band edge flips `voltage_offset` every wake. **This is live in production** — `gisebo-04` reads 5.233 V and is drifting toward the 5.00 V edge; when it arrives its interval thrashes 30 min ↔ 60 min on noise alone. The season machine got 1 °C of hysteresis for exactly this reason and the voltage ladder never did.

Tasks 19–20 fix it: average the ADC (attack the noise at source) *and* add 50 mV of asymmetric hysteresis (absorb the rest). Task 20 is the one that matters — a single-point test cannot see this defect, only a dithering one can.

## Exit criteria

- Both confirmed defects fixed, each with a host test that fails against the old code.
- Voltage bands no longer dither under simulated ADC noise.
- `PrimaryCellPolicy` behaviourally identical to today, proven by host tests.
- The sketch compiles clean and flash/RAM headroom is recorded.
- Nothing shipped to the fleet yet — sprint 01's reflash plan governs that.
