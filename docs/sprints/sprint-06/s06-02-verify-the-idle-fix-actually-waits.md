# S06-02 — Verify the idle fix actually waits

**Estimate:** 2 h
**Backlog item:** TODO #1
**Depends on:** S06-01
**Needs hardware:** YES

## Context

The original defect: `idle(750)` returned early, the DS18B20 read returned the previous conversion, and PROD reported temperatures lagged one interval.

**This cannot be verified on a DEV unit** — the DEV path always used an `os_runloop_once()` loop and was never affected. That asymmetry is why the bug lived for months.

## Steps

1. On the PROD unit, put the sensor in a known bath and change it abruptly between wakes.
2. Confirm the reported temperature tracks the *current* wake, not the previous one.
3. Scope the OneWire line if available: confirm the conversion completes before the read.
4. Confirm the first reading after a cold boot is real, not 85 °C.

## Done when

- [ ] Readings track the current wake.
- [ ] No 85 °C on first boot.
- [ ] Verified on PROD, not DEV.
