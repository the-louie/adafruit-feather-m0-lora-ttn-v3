# S01-06 — Establish whether V5 shares the idle 750 defect

**Estimate:** 1-2 h
**Backlog item:** TODO #14
**Depends on:** S01-05
**Needs hardware:** no

## Context

The `idle(750)` truncation is confirmed for v6 (repo source). Whether `gisebo-04`'s V5 firmware has it is **unknown**, and it decides whether that unit's entire temperature history is time-shifted. V5 has no interval byte, so even retroactive correction would depend on an assumed 5-minute cadence.

## Steps

1. If V5 source was found (S01-05): grep for `LowPower.idle` and check the argument.
2. If not: the question is unanswerable from source. Record that gisebo-04's history is uninterpretable, and say so plainly rather than assuming it is fine.
3. Either way, resolve a confound in the data: gisebo-04 reads 8–9 °C in mid-July while gisebo-01 reads 17–19 °C. Different depth or different site is the likely explanation — confirm which, because 'the sensor lags' and 'the sensor is deeper' are not distinguishable without knowing the deployment.

## Done when

- [ ] V5's idle behaviour established, or explicitly recorded as unknowable.
- [ ] gisebo-04's deployment (site, depth) documented so its readings can be sanity-checked at all.
