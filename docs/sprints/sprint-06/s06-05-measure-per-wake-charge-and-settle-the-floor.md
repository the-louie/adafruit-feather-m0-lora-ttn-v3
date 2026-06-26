# S06-05 — Measure per-wake charge and settle the floor

**Estimate:** 2 h
**Backlog item:** TODO #11
**Depends on:** S06-04
**Needs hardware:** YES

## Context

**The 35× disagreement.** The brief's figures imply ~0.075 mAh per wake; a model of the wake (750 ms conversion plus an occasional 3 s TX) suggests ~0.002 mAh. At 5-minute sampling that is 28 mAh/day versus 7.6 mAh/day — the difference between eating most of the harvest and not noticing.

The index-2 floor was chosen without this number.

## Steps

1. Measure charge for a wake without TX (sensor conversion only).
2. Measure charge for a wake with TX, including the RX windows.
3. Compute daily consumption at index 2 and index 4.
4. Compare against the S01-12 revised harvest figure.
5. **Confirm or revise the floor**, and update the design doc either way.

## Done when

- [ ] Per-wake charge measured for both cases.
- [ ] The 35× disagreement resolved.
- [ ] Floor confirmed or revised with numbers.
