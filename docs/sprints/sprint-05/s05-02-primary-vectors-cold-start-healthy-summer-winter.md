# S05-02 — Primary vectors cold start healthy summer winter

**Estimate:** 2 h
**Backlog item:** TODO #3
**Depends on:** S05-01, S02-06
**Needs hardware:** no

## Context

Three vectors pinning the primary variant's real operating points, with the post-fix uplink-counter semantics.

## Steps

1. **Cold start**: interval index 2 (the `setup()` default), one real temperature then five `250` nulls — the genuine fast-flush shape, which now happens once per join rather than every 16 wakes.
2. **Healthy summer**: index 4, battery ≥5.0 V, six temperatures ≥16 °C. Use gisebo-01's real values (5.768 V, 16.8 °C, index 4) — a production-anchored vector beats an invented one.
3. **Winter on a tired pack**: index 9 or 10 (Winter base 7 + a 2–3 step penalty), battery in the 3.5–4.3 V band, six sub-8 °C temperatures.
4. For each: FPort, hex, exact expected JSON, derived from the decoder rather than by hand.

## Done when

- [ ] Three vectors, all passing.
- [ ] The summer vector uses real production values.
