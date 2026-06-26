# S05-04 — Solar vector full sun full pack

**Estimate:** 2 h
**Backlog item:** TODO #3
**Depends on:** S04-15
**Needs hardware:** no

## Context

**The single most important vector in the file.** Panel current ≈0 mA with a high bus voltage: the charge-terminated case, which looks identical to darkness if you only read current.

With the claimed surplus this is most of the summer — the normal case, not an edge case. If anyone ever 'simplifies' the policy to key on current, this vector is what fails.

## Steps

1. Vector: bus voltage high (~5 V), current ~0 mA, EWMA high, battery healthy, bonus active, interval at floor index 2.
2. Assert the decoder reports sunny-and-full, not dark.
3. Contrast with a night vector (S05-05): near-zero bus voltage, current 0. Same current, opposite meaning — that contrast *is* the test.

## Done when

- [ ] Charge-terminated case decodes unambiguously.
- [ ] Sits directly beside the night vector, with a comment explaining why.
