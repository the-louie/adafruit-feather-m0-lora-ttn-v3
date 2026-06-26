# S02-01 — Choose and implement the idle 750 fix

**Estimate:** 2 h
**Backlog item:** TODO #1
**Depends on:** sprint-01
**Needs hardware:** no

## Context

Confirmed: `setAlarmIn` does `rtc.setAlarmEpoch(now + millis/1000)`, so `idle(750)` sets the alarm to the current second and returns early. The DS18B20 read then returns the *previous* conversion — a one-interval lag on every PROD reading.

## Steps

Pick one:

1. **`delay(750)`** — safe here specifically: the radio is idle during sensor conversion, so the no-`delay()`-near-the-radio rule (master-plan, domain-knowledge) does not apply. Costs ~750 ms of run-mode current per wake.
2. **`idle()` looped against `rtc.getEpoch()`** — respects second granularity, awkward for a 750 ms wait.
3. **9-bit DS18B20 resolution** — 94 ms conversion, 0.5 °C steps. The payload quantizes to 0.2 °C anyway, so this loses less than it appears and cuts awake time 8×.

Decide on measured power cost — but note quiescent draw dominates the budget (TODO #11), so option 1's run-mode cost is likely irrelevant. **Recommend option 1** for its bluntness: it cannot fail the way the current code fails.

Add a comment stating why `delay()` is permitted here, or the next reader will 'fix' it back.

## Done when

- [ ] PROD conversion wait actually waits ≥750 ms.
- [ ] The reason `delay()` is acceptable here is in a comment.
- [ ] DEV path unchanged.
