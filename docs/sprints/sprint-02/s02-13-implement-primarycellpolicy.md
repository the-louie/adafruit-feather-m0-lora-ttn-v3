# S02-13 — Implement PrimaryCellPolicy

**Estimate:** 2 h
**Backlog item:** TODO #4
**Depends on:** S02-12
**Needs hardware:** no

## Context

Today's algorithm, moved. 6 V pack bands (5.0 / 4.3 / 3.5 V), 9-byte payload, FPorts 10/20.

**This must be behaviourally identical to the current code.** Production data confirms the current algorithm works; any difference is a bug introduced by this refactor.

## Steps

1. Move `calculate_interval_index`'s voltage-offset ladder into the policy.
2. Keep the clamp to [1, 10].
3. Keep the two write points for `currentIntervalIndex`: setup, and post-`EV_TXCOMPLETE`. That invariant is what makes byte 0 mean 'the interval these six samples were taken at'.
4. Do not 'improve' anything while moving it.

## Done when

- [ ] Identical behaviour to pre-refactor, proven by S02-14.
- [ ] The two-write-point invariant preserved.
