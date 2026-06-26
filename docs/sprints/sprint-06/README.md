# Sprint 06 — Core on-device verification

**Dates:** 2026-09-24 (Thu) → 2026-10-07 (Wed) — **provisional, gated on delivery**
**Capacity:** 1 developer, ~30 h effective
**Planned:** 12 tasks, ~24 h
**Status:** 🟡 Hardware ordered (Feather M0 + DS18B20 + INA219 + panel + 18650 pack), ETA later than sprint 03

## Goal

Verify on real hardware everything sprints 02–04 could only verify by compiling. This sprint covers the **core** — the parts that need only a Feather and a DS18B20. Solar bring-up is sprint 07.

## What we are paying for by verifying this late

The hardware ETA is later than sprint 03, so verification could not be folded into the sprints that make the changes. The concrete cost, stated so it is not a surprise:

- **Sprint 04 picks the index-2 floor on an unmeasured number.** The per-wake energy estimates disagree by 35× (~0.075 mAh from the brief's figures vs ~0.002 mAh from a model of the wake). Task 05 settles it — after the decision was already made. Expect to revisit the floor (task 09).
- **Sprint 03 ships the RTC ownership seam undocumented-as-safe.** `ArduinoLowPower` and `RTCZero` both own the RTC; whether double-`begin()` collides was unknowable at the time. Task 03 finds out.
- Everything in sprints 02–04 reaches production having been verified by a compiler.

## Two constraints that survive having hardware

**The `idle(750)` bug is PROD-only.** It cannot be verified on a DEV-strapped unit — the DEV path always used an `os_runloop_once()` loop and was never affected. Verification needs a **strapped PROD unit with USB detached**, which means no serial and debugging over LoRa. That asymmetry is exactly how the bug survived for months.

**Solar cannot be observed in DEV.** USB puts 5 V on the pin the panel feeds, so the Schottky blocks the panel and the INA219 reads ~0 mA on a 5 V bus. That is why solar bring-up (sprint 07) runs on a bench PSU with telemetry over the air.

## Working agreement

- **Commit in small batches**: one function + its test (if available) + its documentation, per commit.
- Every finding gets a dated dev-note — **especially the ones that contradict what we assumed**. That is the entire point of this sprint.

## Task index

| # | Task | Est | Item |
|---|---|---|---|
| 01 | Bench bring-up of a PROD-strapped unit | 2 h | 12 |
| 13 | Capture a real reboot and confirm the 252 signature | 1 h | 1 |
| 02 | Verify the idle fix actually waits | 2 h | 1 |
| 03 | Verify ArduinoLowPower and RTCZero do not collide | 2 h | 6 |
| 06 | Verify noinit survives a real reset | 2 h | 5 |
| 07 | Verify DeviceTimeReq lands through a real gateway | 2 h | 6 |
| 04 | Measure sleep current against the budget | 2 h | 11 |
| 05 | Measure per-wake charge and settle the floor | 2 h | 11 |
| 09 | Revisit the floor decision with measured data | 2 h | 8 |
| 10 | Remediation buffer for verification findings | 4 h | 12 |
| 11 | Re-verify after remediation | 2 h | 12 |
| 14 | Dev-note core on-device findings | 2 h | 12 |

Do task 13 **first** after bring-up: it flashes pre-fix firmware and either confirms or overturns the original `idle(750)` diagnosis, which everything in sprint 02 was built on.

## Exit criteria

- The `idle(750)` diagnosis is confirmed or overturned against real hardware.
- Per-wake charge is measured and the floor is confirmed or revised.
- The RTC seam is proven safe or a fallback is adopted.
- Every assumption sprints 02–04 could not test is confirmed or corrected, and the corrections are made.
