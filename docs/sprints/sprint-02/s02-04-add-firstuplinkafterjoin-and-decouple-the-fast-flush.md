# S02-04 — Add firstUplinkAfterJoin and decouple the fast-flush

**Estimate:** 2 h
**Backlog item:** TODO #2
**Depends on:** sprint-01
**Needs hardware:** no

## Context

Confirmed from 107 production uplinks: `wakeCounter == 1` fires every 16 wakes, not once per join, because the counter is 4-bit and wraps. A third of all uplinks carry a 4-sample batch with two wasted slots.

The fix is to express the actual intent rather than infer it from a counter.

## Steps

1. Add `static bool firstUplinkAfterJoin`, set on `EV_JOINED`.
2. Change the send condition at `:433` to `firstUplinkAfterJoin || ramCount >= batchTarget`.
3. Clear it after the first successful TX.
4. Consider whether it belongs in `.noinit` (sprint 03) — a join-failure reset re-joins, so it should re-arm naturally.
5. Fix the misleading `"FAST-FLUSH"` comment block at `:430-433`, which describes behaviour the code has never had.

## Done when

- [ ] Fast-flush fires exactly once per join.
- [ ] `wakeCounter` no longer gates the send decision.
- [ ] The comment matches the code.
