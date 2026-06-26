# S03-14 — Clock-valid flag and degraded mode

**Estimate:** 2 h
**Backlog item:** TODO #6
**Depends on:** S03-12
**Needs hardware:** no

## Context

**No clock is a supported state**, not an error. Until `DeviceTimeReq` lands — or forever, in poor downlink coverage — the solar policy runs on the raw EWMA without day-length normalisation.

Season is temperature-based regardless, so nothing else is affected. That is a direct consequence of keeping season on water temperature rather than day length.

## Steps

1. Wire the clock-valid flag into the status byte (bit 2).
2. Ensure the solar policy degrades to the raw EWMA when the clock is invalid, rather than misbehaving.
3. Confirm the season machine is genuinely independent of the clock — it must be.
4. Make sure a unit that never acquires a clock is *visible* rather than silently degraded (backend alarm, sprint 05).

## Done when

- [ ] Degraded path works and is tested.
- [ ] Season demonstrably clock-independent.
- [ ] The degraded state is reported, not hidden.
