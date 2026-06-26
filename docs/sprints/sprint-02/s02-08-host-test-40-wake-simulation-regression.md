# S02-08 — Host test 40-wake simulation regression

**Estimate:** 2 h
**Backlog item:** TODO #2
**Depends on:** S02-04, S02-07
**Needs hardware:** no

## Context

**This is the regression test for the confirmed fast-flush defect, and the current code fails it.** Write it so it fails against the old behaviour first, then confirm the fix turns it green — otherwise it is not testing what it claims.

## Steps

1. Simulate 40 wakes through the send-decision logic.
2. Assert: exactly one fast-flush, at the join; every subsequent uplink carries a full 6-sample batch.
3. Assert: the uplink counter increments by exactly 1 per uplink.
4. Bonus — reproduce the old behaviour and assert the observed production table ({1,4}, {7,6}, {13,6}). That pins the defect in a test so the diagnosis outlives the chat log.

## Done when

- [ ] Test fails against pre-fix code, passes after.
- [ ] The old {1,7,13} cycle is captured as a documented historical test.
