# S03-09 — Add an RTCZero instance for reads

**Estimate:** 2 h
**Backlog item:** TODO #6
**Depends on:** sprint-02
**Needs hardware:** no

## Context

The Feather M0 carries an external 32.768 kHz crystal and the Arduino SAMD variant does not define `CRYSTALLESS`, so `RTCZero` sources GCLK2 from it: ~20–50 ppm.

**This is already in use** — `ArduinoLowPower` wraps `RTCZero` internally, so `deepSleep()` is already crystal-backed. This task adds the ability to *read* the clock, which the EWMA decay needs. It does not change sleeping.

## Steps

1. Add an `RTCZero` instance for `getEpoch()`.
2. Keep `ArduinoLowPower` owning sleep and idle — do not restructure the proven sleep path.
3. Replace any elapsed-time reasoning with `getEpoch()` deltas. An earlier draft summed `sleepIntervalSeconds` to reconstruct elapsed time; that is strictly worse (misses awake time, cannot survive a reset) and must not reappear.

## Done when

- [ ] `getEpoch()` readable.
- [ ] Sleep path untouched.
- [ ] No hand-rolled elapsed-time accumulator anywhere.
