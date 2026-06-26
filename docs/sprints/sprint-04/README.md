# Sprint 04 — Solar policy

**Dates:** 2026-08-27 (Thu) → 2026-09-09 (Wed)
**Capacity:** 1 developer, ~30 h effective
**Planned:** 19 tasks, ~32 h — gained the day-length decoder tasks from sprint 03; shed clarity-ratio and out-of-contract handling to sprint 05

## Goal

Build `SolarPolicy` on the foundations from sprint 03, and teach the decoder to read it.

## The two ideas this sprint rests on

**The INA219 measures harvest, not sunlight.** It sits in the charging path, so when the pack is full the charger terminates and current collapses to ~0 — in full summer sun. With the claimed surplus that is most of the summer. So the policy keys on **panel bus voltage** (`sun_present = bus_mV > 3000`), which reads the charger's operating point or the panel's open-circuit voltage, and is near 0 V only when genuinely dark. Current is still uplinked and accumulated, for energy analysis — it just is not the decision input.

**The window is time-based, not wake-based.** A window of N wakes is a window of N × interval seconds, and the interval is what the window controls — so a sunny afternoon shortens the interval, shrinking the window to hours that are all daylight, and it never sees the night that should pull the average down. It hunts on a multi-day period. Take `dt` from the RTC and decay properly.

## Risk carried into this sprint

The index-2 floor rests on an unmeasured number. The brief's figures imply ~0.075 mAh per wake; a model of the wake suggests ~0.002 mAh — a 35× disagreement, which at 5-minute sampling is 28 mAh/day versus 7.6 mAh/day. The harvest accumulator will settle it from field data. Until then index 2 is defensible mainly because the healthy-battery gate self-corrects.

## Working agreement

- **Commit in small batches**: one function + its test (if available) + its documentation, per commit. Several commits per task; never one commit per task.
- Every non-trivial change gets a dated dev-note in `docs/dev-notes/`.
- **No test devices.** Verification is compile-only plus host-side tests. On-device work is parked in sprint 06.

## Task index

| # | Task | Est | Item |
|---|---|---|---|
| 01 | INA219 integration and calibration | 2 h | 8 |
| 02 | INA219 read placement in the conversion window | 2 h | 8 |
| 03 | INA219 power-down between reads | 1 h | 8 |
| 04 | Sun-presence EWMA with RTC-derived dt | 2 h | 8 |
| 05 | Host tests EWMA decay | 2 h | 8 |
| 06 | Harvest accumulator | 1 h | 8 |
| 07 | SolarPolicy skeleton and li-ion bands | 2 h | 8 |
| 08 | Solar interval decision bonus and floor | 2 h | 8 |
| 09 | Host tests solar interval ladder and self-correction | 2 h | 8 |
| 10 | Status byte assembly | 2 h | 8 |
| 11 | Solar payload append | 2 h | 8 |
| 12 | Host tests solar payload encoding | 2 h | 8 |
| 13 | FPort selection for the solar variant | 1 h | 8 |
| 14 | Decoder accept 15-byte on FPorts 11 and 21 | 2 h | 9 |
| 15 | Decoder parse the solar fields | 2 h | 9 |
| 18 | Dev-note the solar policy | 1 h | 8 |
| 19 | Decoder day length from latitude and date | 2 h | 7 |
| 20 | Host tests decoder day length | 2 h | 7 |
| 21 | Latitude as a per-device decoder constant | 1 h | 7 |

Clarity ratio and out-of-contract byte handling moved to sprint 05 (S05-19/20) — they consume the vectors that live there and block nothing in this sprint.

## Exit criteria

- `SolarPolicy` complete, host-tested, compiling.
- Decoder reads the 15-byte payload and computes clarity.
- Bytes 0–8 still byte-identical to the primary variant.
- Every number the policy decides on is visible in telemetry — uplinks are the only instrument.
