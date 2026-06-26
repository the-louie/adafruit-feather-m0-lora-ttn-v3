# S03-18 — Dev-note RTC and timekeeping

**Estimate:** 1 h
**Backlog item:** TODO #6
**Depends on:** S03-14
**Needs hardware:** no

## Context

Convention: one dated dev-note per change.

## Steps

1. Record that `ArduinoLowPower` already wrapped `RTCZero` — sleeping was always crystal-backed; this sprint made the clock readable.
2. Record the ownership seam and that it is unverified without hardware.
3. Record the GPS→UTC constants and the RTC-does-not-survive-reset finding.

## Done when

- [ ] Dev-note committed.
- [ ] The unverified seam is called out as a known risk, not buried.
