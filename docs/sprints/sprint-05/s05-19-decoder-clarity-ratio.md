# S05-19 — Decoder clarity ratio

**Estimate:** 2 h
**Backlog item:** TODO #9
**Depends on:** S04-15, S03-15
**Needs hardware:** no

## Context

`clarity = ewma / expected_daylight_fraction`. ~1.0 is clear skies; persistently low is overcast — **or snow, leaves, or shade on the panel**. A vertical mount in July with a low EWMA looks plausible until you divide by what the EWMA should have been.

Costs zero payload bytes: the backend has the uplink timestamp and the latitude constant.

## Steps

1. Compute expected daylight fraction from the uplink timestamp and the latitude constant.
2. Emit `clarity` alongside the raw EWMA — never instead of it. The raw value is what the firmware actually used.
3. Emit `null` when the clock-valid flag is false; a clarity ratio computed from an unseeded clock is nonsense.

## Done when

- [ ] Clarity computed and emitted.
- [ ] Raw EWMA still emitted.
- [ ] Null when the clock is invalid.
