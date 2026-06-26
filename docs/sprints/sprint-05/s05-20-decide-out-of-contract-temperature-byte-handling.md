# S05-20 — Decide out-of-contract temperature byte handling

**Estimate:** 2 h
**Backlog item:** TODO #3
**Depends on:** S01-03
**Needs hardware:** no

## Context

The decoder's temperature chain handles 250/251/252 and `v <= 200` — so bytes **201–249 and 253–255 match no branch** and are silently dropped, shortening the array and misaligning every later reading against its extrapolated timestamp.

Not reachable from current firmware, so not a live bug. But the live decoder also drops nulls entirely, which means array position already does not imply sample index — worth resolving both together.

## Steps

1. Decide: document as out-of-contract, or push `null`.
2. **Do not paper over it** by choosing vectors that avoid the range.
3. Consider the interaction with the live decoder's null-omission: if entries carry their own timestamps, position may not matter — confirm rather than assume.
4. Add vectors for the whole 0–255 byte range either way.

## Done when

- [ ] Decision made and documented.
- [ ] Full byte range covered by vectors.
- [ ] The interaction with timestamped entries is resolved explicitly.
