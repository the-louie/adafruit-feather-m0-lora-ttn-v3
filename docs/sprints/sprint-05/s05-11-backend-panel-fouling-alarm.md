# S05-11 — Backend panel fouling alarm

**Estimate:** 2 h
**Backlog item:** TODO #10
**Depends on:** S05-19
**Needs hardware:** no

## Context

A vertical panel sheds snow and rain by design — but leaves, shade, and dirt still foul it. A fouled panel in July shows a plausible-looking low EWMA that only becomes obviously wrong when divided by what it should have been.

## Steps

1. Alarm when clarity stays well below 1.0 against a high expected daylight fraction.
2. Require persistence — a week of genuine overcast is weather, not fouling.
3. Suppress when the clock is invalid (clarity is null then).

## Done when

- [ ] Alarm live with a persistence window.
- [ ] Weather does not trigger it.
- [ ] Suppressed when clarity is null.
