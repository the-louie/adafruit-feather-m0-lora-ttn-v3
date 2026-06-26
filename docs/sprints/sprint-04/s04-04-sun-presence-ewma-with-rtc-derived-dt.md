# S04-04 — Sun-presence EWMA with RTC-derived dt

**Estimate:** 2 h
**Backlog item:** TODO #8
**Depends on:** S03-09
**Needs hardware:** no

## Context

The core solar signal. `sun_present = (bus_mV > 3000)`, decayed with a 24 h time constant against real elapsed time from the RTC.

At night the panel is dark, the Schottky blocks, and the bus sits near 0 V. With sun, the bus reads either the charger's operating point (~4.5–5 V) or panel Voc once charge terminates. So this is a clean discriminator that does **not** collapse when the battery is full — which current does.

## Steps

1. `alpha = 1 - exp(-dt / TAU)` with `TAU = 86400`; `ewma += alpha * (sun_present - ewma)`.
2. `dt` from `rtc.getEpoch()` deltas — never from summed intervals.
3. **Do not use the linear approximation** `alpha ≈ dt/TAU`. It errs ~13% at a 6 h interval, which is exactly the winter case. `exp()` once per wake is free at this duty cycle.
4. Persist the EWMA in `.noinit`.

## Done when

- [ ] EWMA decays against real elapsed time.
- [ ] `exp()` used, not the linear approximation.
- [ ] Survives a soft reset.
