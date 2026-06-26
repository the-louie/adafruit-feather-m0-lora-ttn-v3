# S01-16 — Quantify historical impact of the lag

**Estimate:** 2 h
**Backlog item:** TODO #1
**Depends on:** S01-06
**Needs hardware:** no

## Context

Every PROD reading taken by a v6 unit is time-shifted one interval late. `gisebo-01` is v6. The live decoder extrapolates per-sample timestamps from the uplink time — so it is confidently labelling each sample with a timestamp that is one interval wrong.

This decides whether historical data can be salvaged or must be caveated.

## Steps

1. Establish how far back gisebo-01 has run v6 — earlier data will show when the payload length changed from 8 to 9.
2. Assess retroactive correction: shift each v6 series back one interval. Byte 0 records the interval, so the shift is computable per message, but only where the interval was stable across the shift.
3. For gisebo-04 (V5, no interval byte) correction depends entirely on the assumed 5-minute cadence. Record whether that is safe.
4. Recommend: correct, caveat, or discard.

## Done when

- [ ] Affected date range established per unit.
- [ ] Correction feasibility assessed and recommended.
- [ ] Downstream consumers of this data are told.
