# S04-12 — Host tests solar payload encoding

**Estimate:** 2 h
**Backlog item:** TODO #8
**Depends on:** S04-11
**Needs hardware:** no

## Context

Encoding bugs are silent and permanent — they corrupt data nobody can re-collect.

## Steps

1. Round-trip every field: encode a known value, decode it, compare.
2. Test the clamps at both ends of each field.
3. Test bytes 0–8 identity against the primary variant with the same inputs.
4. Test the charge-terminated case explicitly: current ≈ 0 with a high bus voltage must encode and decode as 'sunny, full battery' — not as darkness. **This is the single most important case in the design.**

## Done when

- [ ] Every field round-trips.
- [ ] Clamps verified.
- [ ] The charge-terminated case is unambiguous end to end.
