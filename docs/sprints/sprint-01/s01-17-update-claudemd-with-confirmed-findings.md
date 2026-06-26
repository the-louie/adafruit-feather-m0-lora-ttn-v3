# S01-17 — Update CLAUDE.md with confirmed findings

**Estimate:** 1-2 h
**Backlog item:** TODO #—
**Depends on:** S01-10, S01-11
**Needs hardware:** no

## Context

CLAUDE.md documents the protocol and design rules and is the first thing anyone — human or agent — reads. Several statements in it are now known to be wrong.

## Steps

1. Record that `rebootDetected` is dead and sequence only ever takes {1, 7, 13}.
2. Record that the fast-flush fires every 16 wakes, not once per join.
3. Record the `idle(750)` defect and the one-interval lag.
4. Record that the canonical decoder is the live one, and how to run the test harness.
5. Record that the fleet runs two protocol versions.

## Done when

- [ ] CLAUDE.md reflects reality rather than intent.
- [ ] The test harness documented.
- [ ] No statement in it contradicts the production data.
