# S05-16 — Fleet reflash execution plan

**Estimate:** 1-2 h
**Backlog item:** TODO #14
**Depends on:** S01-07, S05-08
**Needs hardware:** no

## Context

Sprint 01 made the plan; this makes it executable now that the firmware exists.

**With no test devices, the first unit flashed is a production unit.** That risk was accepted in S01-07 — this is where it becomes real.

## Steps

1. Confirm the order and the decoder-change sequencing from S01-07.
2. Decide which unit goes first, and why. gisebo-04 is the V5 unit with no source, so it has less to lose; gisebo-01 already runs v6 and is the better comparison baseline. Argue it.
3. Define rollback: if a unit goes silent after reflash, what happens? At a post at a lake, the answer may be 'a site visit', and that should be known before, not after.
4. Schedule against site access.

## Done when

- [ ] Order decided with reasoning.
- [ ] Rollback defined honestly, including 'site visit'.
- [ ] Scheduled against real access windows.
