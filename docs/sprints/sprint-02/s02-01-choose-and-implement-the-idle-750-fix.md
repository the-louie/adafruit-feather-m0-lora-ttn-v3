# S02-01 — Choose and implement the idle 750 fix

**Estimate:** 2 h
**Backlog item:** TODO #1
**Depends on:** sprint-01
**Needs hardware:** no

## Context

Confirmed: `setAlarmIn` does `rtc.setAlarmEpoch(now + millis/1000)`, so `idle(750)` sets the alarm to the current second and returns early. The DS18B20 read then returns the *previous* conversion — a one-interval lag on every PROD reading.

## Steps

**Decided 2026-07-17: option 1, `delay(750)`.** Recorded here because this task previously said "pick one" and deferred to a measurement (S06-05) that lands two sprints *after* this ships.

1. **`delay(750)`** — safe here specifically: the radio is idle during sensor conversion, so the no-`delay()`-near-the-radio rule (master-plan, domain-knowledge) does not apply. Costs ~750 ms of run-mode current per wake.
2. **`idle()` looped against `rtc.getEpoch()`** — respects second granularity, awkward for a 750 ms wait.
3. **9-bit DS18B20 resolution** — 94 ms conversion, 0.5 °C steps. **Rejected.** It leaves only ~26 ms around the INA219's ~68 ms averaging (S04-01/02), coupling sensor resolution to averaging so neither can be tuned alone — and *lengthening* the averaging is exactly S07-05's remedy if motorboating proves slower than 68 ms. Its only advantage is power, and that argument rests on the same unmeasured per-wake figure that already undermines the index-2 floor, while ~290 µA of quiescent draw likely dominates run-mode cost anyway.

Option 1 wins on bluntness: it cannot fail the way the current code fails, and it leaves 682 ms of slack in the window.

Add a comment stating why `delay()` is permitted here, or the next reader will 'fix' it back.

## Done when

- [ ] PROD conversion wait actually waits ≥750 ms.
- [ ] The reason `delay()` is acceptable here is in a comment.
- [ ] DEV path unchanged.
