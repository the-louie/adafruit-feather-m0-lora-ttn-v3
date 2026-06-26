# S04-06 — Harvest accumulator

**Estimate:** 1 h
**Backlog item:** TODO #8
**Depends on:** S04-04
**Needs hardware:** no

## Context

Integrated mAh since cold boot, 16-bit at 1 mAh/LSB. **It wraps, and an earlier draft claiming "decades of headroom" was wrong.**

At energy balance, harvest ≈ consumption ≈ 7–28 mAh/day (the 35× per-wake disagreement sets the range). 65535 mAh is therefore **6.4 years at the high end** and ~25 years at the low — so it can wrap well inside the 5–10 year replacement window.

Do **not** coarsen the LSB to fix this: at 10 mAh/LSB a day's harvest is 1–3 LSB, which destroys the daily resolution the pack-health trend needs. Keep 1 mAh/LSB and let the backend unwrap — a wrap is unambiguous (the value decreases), uplinks arrive every 30 min, and you would have to lose 65535 mAh of uplinks to miss one.

This is the number that settles the unmeasured per-wake energy question (28 vs 7.6 mAh/day) from field data, and it feeds the pack-health trend that makes the 5–10 year replacement cycle actionable. It is not decoration.

## Steps

1. Accumulate `current × dt` per wake into the `.noinit` struct.
2. Cumulative since cold boot, surviving soft resets.
3. Watch integer overflow and the mA→mAh scaling.

## Done when

- [ ] Accumulates correctly across wakes.
- [ ] Survives a soft reset, resets on cold boot.
- [ ] Scaling verified by a host test.
