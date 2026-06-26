# S07-09 — Verify the charge-terminated case end to end

**Estimate:** 2 h
**Backlog item:** TODO #8
**Depends on:** S07-04
**Needs hardware:** YES

## Context

**The case the entire solar signal design exists for.** With a full pack in full sun, the charger terminates and input current collapses to ~0 — identical to darkness if you only read current. With the claimed surplus this is most of the summer: the normal case, not an edge case.

The policy keys on bus voltage precisely to survive this. Prove it on hardware.

## Steps

1. Full pack, PSU up. Confirm current drops to ~0 while bus voltage stays high.
2. Confirm the policy still reads it as sunny — the EWMA must stay high.
3. Contrast with genuine darkness: PSU off, bus near 0 V, current 0. Same current, opposite meaning.
4. Confirm the decoder reports the two cases distinguishably (S05-04's vector, now against real hardware).

## Done when

- [ ] Charge-terminated case reads as sunny, not dark.
- [ ] Genuine darkness reads as dark.
- [ ] The distinction survives end to end into the decoder.
