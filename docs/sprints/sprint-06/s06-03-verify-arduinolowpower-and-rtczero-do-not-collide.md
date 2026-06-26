# S06-03 — Verify ArduinoLowPower and RTCZero do not collide

**Estimate:** 2 h
**Backlog item:** TODO #6
**Depends on:** S06-01
**Needs hardware:** YES

## Context

Both libraries wrap the same RTC. `ArduinoLowPower` holds its own `RTCZero` and calls `begin()` lazily; sprint 03 added a second instance for reads. Whether double-`begin()` is idempotent, and what happens when both touch the alarm, is **unknown** — it was flagged as unverifiable in S03-10.

A collision here would break sleep timing, which breaks everything.

## Steps

1. Confirm sleep intervals are still accurate over many cycles — compare uplink timestamps against the interval byte.
2. Confirm `getEpoch()` returns sane values across sleeps.
3. Confirm the wake alarm still fires after a `getEpoch()` read.
4. Test the specific worry from S03-10: does the read instance's `begin()` disturb the sleep instance's alarm config?

## Done when

- [ ] Sleep timing accurate across many cycles.
- [ ] Clock readable without disturbing wake.
- [ ] The S03-10 seam is confirmed safe or a fallback is adopted.
