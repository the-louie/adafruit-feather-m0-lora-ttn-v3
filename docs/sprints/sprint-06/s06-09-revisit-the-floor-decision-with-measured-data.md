# S06-09 — Revisit the floor decision with measured data

**Estimate:** 2 h
**Backlog item:** TODO #8
**Depends on:** S06-05
**Needs hardware:** YES

## Context

Sprint 04 chose the index-2 (5 min) floor while the per-wake energy figure was unmeasured and the two available estimates disagreed by **35×** — 28 mAh/day versus 7.6 mAh/day at 5-minute sampling. That was defensible only because the healthy-battery gate self-corrects if the floor turns out to be net-negative.

Task 05 measures the real number. This is where the decision gets made properly.

## Steps

1. Take the measured per-wake charge (S06-05) and the measured/estimated harvest (S01-12, S07 charger work).
2. Compute the real daily balance at index 2 and index 4.
3. If index 2 is net-negative in any season, revise the floor — the self-correcting gate would otherwise leave the unit hunting on a multi-day period rather than settling.
4. Update `docs/solar-variant-design.md` with measured numbers replacing estimates, and delete the 35×-disagreement caveat once it is resolved.
5. If the floor changes, check the host tests in S04-09 still express the intended behaviour.

## Done when

- [ ] Real daily energy balance computed at the floor.
- [ ] Floor confirmed or revised, with numbers.
- [ ] Design doc carries measurements, not estimates.
