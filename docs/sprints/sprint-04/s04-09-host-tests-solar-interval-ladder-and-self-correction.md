# S04-09 — Host tests solar interval ladder and self-correction

**Estimate:** 2 h
**Backlog item:** TODO #8
**Depends on:** S04-08
**Needs hardware:** no

## Context

The self-correcting loop is an argument, not yet a fact. Test it — it is what the floor decision rests on.

## Steps

1. Test: healthy battery + EWMA 0.65 + Summer → index 2 (floor). Fall → 3. Winter → 5.
2. Test the **hysteresis band**, not a threshold: 0.54 does not engage; 0.56 engages; then 0.46 stays engaged; 0.44 releases. The latch is the point — a bare 0.5 flaps daily because the EWMA ripples ±0.11 and Fall/Spring peaks at 0.522 every afternoon.
3. Test the ripple case directly: drive a simulated Fall day/night cycle (9.6 h light) for a week and assert the bonus **never engages**. That is the defect the band exists to prevent, and it is invisible to a single-point test.
3. Test: low battery + high EWMA → **no bonus**, seasonal base. The gate is the safety property; prove it.
3. Test: simulate net-negative energy — battery drains, bonus disappears, interval returns to base. Assert it converges rather than oscillating unboundedly.
4. Test the clamp at both ends.
5. Test: no clock → raw EWMA path still produces a sane interval.

## Done when

- [ ] The healthy-battery gate is pinned.
- [ ] Self-correction demonstrated, with bounded oscillation.
- [ ] Degraded no-clock path tested.
