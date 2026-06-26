# S01-11 — Dev-note fast-flush misfire and dead rebootDetected

**Estimate:** 1-2 h
**Backlog item:** TODO #2
**Depends on:** none
**Needs hardware:** no

## Context

Both defects are confirmed from 107 production uplinks across two devices. Write it up with the table — it is the most convincing evidence in the project and should not live only in a chat log.

## Steps

1. Record the observed distribution: sequence ∈ {1, 7, 13} only; batch size 4 for seq 1, 6 for seq 7 and 13; 31/32/29 on gisebo-04 and 5/5/5 on gisebo-01.
2. Record the mechanism: `wakeCounter` is 4-bit, wraps to 1 every 16 wakes, and re-triggers the `wakeCounter == 1` fast-flush with a partial batch. Simulating `loop()` reproduces the table exactly.
3. Record that `rebootDetected` is therefore dead — sequence is never 0 — and that the off-by-one fix does not work either (the cycle would become 0, 6, 12, which includes 0).
4. Note that `doc/test-payloads.md` vector 1 asserts `sequence 0` on the fast-flush: the stale doc records intended behaviour the code never had, and is how the defect surfaced.

## Done when

- [ ] Dev-note committed with the production table.
- [ ] Mechanism explained well enough that the fix is obvious to the next reader.
- [ ] {1,7,13} recorded as the fleet-tracking signature for un-reflashed units.
