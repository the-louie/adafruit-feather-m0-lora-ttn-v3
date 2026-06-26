# S03-10 — Document the ArduinoLowPower and RTCZero ownership seam

**Estimate:** 1 h
**Backlog item:** TODO #6
**Depends on:** S03-09
**Needs hardware:** no

## Context

Both libraries wrap the same RTC peripheral. `ArduinoLowPower` holds its own `RTCZero` instance and calls `begin()` lazily via `attachInterruptWakeup(RTC_ALARM_WAKEUP, ...)`; adding a second instance means `begin()` runs twice and alarm configuration can collide.

**This cannot be verified without hardware** — it is the highest-risk unverifiable change in the plan.

## Steps

1. Read both libraries' `begin()` and alarm paths and write down exactly what is shared.
2. Document whether double-`begin()` is idempotent, and what happens if both set an alarm.
3. Add the on-device verification task to sprint 06 with a specific thing to check, not 'test the RTC'.
4. If the risk looks real, consider reading the RTC registers directly instead of instantiating a second `RTCZero`.

## Done when

- [ ] The shared-state analysis is written down.
- [ ] A specific sprint-06 verification task exists.
- [ ] A fallback is identified if the seam turns out to be unsafe.
