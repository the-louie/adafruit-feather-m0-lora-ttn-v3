# S05-03 — Primary vectors uplink counter continuity and gaps

**Estimate:** 1-2 h
**Backlog item:** TODO #3
**Depends on:** S05-02
**Needs hardware:** no

## Context

The counter's whole purpose after S02-05 is drop detection: consecutive uplinks differ by exactly 1, so a gap means a lost message. Prove the decoder surfaces that.

## Steps

1. Vectors for consecutive uplinks differing by 1.
2. A vector pair with a gap — the decoder must make it visible.
3. A wraparound vector (15 → 0), which must **not** read as a reboot or a gap.
4. A historical vector documenting the old {1, 7, 13} wake-counter cycle, for the transition period and as a record of the defect.

## Done when

- [ ] Continuity, gap, and wraparound covered.
- [ ] The old semantics are captured for the transition.
