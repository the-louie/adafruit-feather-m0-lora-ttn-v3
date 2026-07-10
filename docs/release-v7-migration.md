# v7 release and migration notes (S05-16, S05-17, S05-18)

## What v7 is

The firmware after sprints 02–04: two power variants behind `PowerPolicy`, both confirmed defects fixed, `.noinit` state, RTC/DeviceTimeReq, and the solar policy. Target device: **gisebo-05** (new, solar).

## Data implications for anyone reading the series

- **Readings before the idle(750) fix are 30 minutes late.** gisebo-01's entire 9-byte history (from 2026-03-09 12:03). gisebo-04 (V5) is **not** affected. gisebo-05 will not be.
- **`sequence` means something different at v7.** It was a wake counter (values {1,7,13}); it is now an **uplink counter** (+1 per successful uplink). The v7 decoder emits it as `uplink_counter`.
- **`interval_index` appears for the first time.** Byte 0 was never decoded before; the backend can now see what the interval algorithm chose.

## The cutover is NOT a reflash

No deployed unit is reflashed. gisebo-01 is frozen then retired; gisebo-04 is untouched; gisebo-05 is born on v7. The v6→v7 counter-semantics ambiguity therefore never arises — nothing reinterprets old bytes. The cutover is a **device swap** (sprint 08), sequenced so telegraf is migrated additively *before* the swap and gisebo-01 comes home intact as the rollback.

## Sprint 06 readiness review (S05-18)

Sprints 06 (core bench verification) and 07 (solar bring-up) are **gated on hardware** — ordered, ETA "later than sprint 03", not firm. They cannot start until a board is on a bench.

**The single highest-value hour when hardware lands: S06-13** — flash pre-fix firmware, cold boot, and finally observe the 252-in-slot-0 signature that 139 production uplinks could not show (no reboot occurred in the retained window). That either confirms or overturns the whole idle(750) diagnosis.

**What sprint 06/07 must verify that host tests cannot:**
- idle(750) actually waits — on a PROD-strapped unit, never DEV (the defect does not exist in DEV).
- ArduinoLowPower + RTCZero coexist on the live RTC without the read instance disturbing the alarm (S03-10).
- `.noinit` survives a real reset, including a brief power interruption not producing a false-valid magic word.
- INA219 wiring, calibration, load-side bus voltage, power-down current.
- The probe fails correctly with a disconnected INA219, and the S01-09 alarm fires.
- The motorboat period vs the 68 ms averaging window (reopens the no-MPPT decision if longer).
- Li-ion's battery-temperature coefficient against the 200 mV solar band spacing.
- Sleep current vs the ~290 µA budget; per-wake charge (settles the 35× floor disagreement).

## State at end of sprint 05

Sprints 01–05 complete. Firmware compiles (69560 B, 26%), 190 host assertions + 46 decoder assertions green. Everything is compile- and host-verified; nothing is on silicon. The plan's honest position: **the bulk is in place and ready to exploit hardware the moment it arrives.**
