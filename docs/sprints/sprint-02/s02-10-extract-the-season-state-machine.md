# S02-10 — Extract the season state machine

**Estimate:** 2 h
**Backlog item:** TODO #4
**Depends on:** S02-07
**Needs hardware:** no

## Context

The season machine is **shared** between both policies — it is not duplicated. It stays keyed to water temperature: day length was considered as a season signal and rejected, because water temperature directly measures the thing the season machine exists to track (how fast the tank is changing), where day length is two proxies removed. See `docs/solar-variant-design.md`.

Production confirms the current logic is correct — gisebo-01 sits at index 4 = Summer base with a healthy battery.

## Steps

1. Move the state machine and its thresholds into `season.h` / `season.cpp`.
2. Keep the 1 °C hysteresis exactly as-is — it is what prevents flapping at the 16 °C and 8 °C boundaries.
3. Preserve the invalid/NaN guard: an out-of-range temp must leave the season state untouched.
4. No behaviour change. None.

## Done when

- [ ] Season logic in its own module.
- [ ] Hysteresis and NaN guard preserved verbatim.
- [ ] Sketch still compiles.
