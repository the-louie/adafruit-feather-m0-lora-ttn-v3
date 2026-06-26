# S05-09 — Backend alarm clock never acquired

**Estimate:** 1-2 h
**Backlog item:** TODO #10
**Depends on:** S04-15
**Needs hardware:** no

## Context

A unit that never acquires a clock runs degraded — raw EWMA, no clarity normalisation — **silently**. In poor downlink coverage that could be permanent.

## Steps

1. Alarm when clock-valid is false after N uplinks post-join.
2. Pick N so a slow acquisition does not page anyone, but a permanent failure surfaces within a day.
3. Document that this degrades the solar policy but not the season machine — the operator needs to know it is not urgent.

## Done when

- [ ] Alarm live.
- [ ] Severity matches the actual impact.
