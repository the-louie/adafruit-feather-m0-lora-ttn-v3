# S03-07 — Host tests noinit validity and versioning

**Estimate:** 2 h
**Backlog item:** TODO #5
**Depends on:** S03-05, S03-06
**Needs hardware:** no

## Context

The version guard exists to prevent a specific catastrophe — resuming from a stale struct layout. Test it, or it is decoration.

## Steps

1. Test: valid magic + valid version → restore.
2. Test: valid magic + **wrong version** → cold boot. This is the one that matters.
3. Test: garbage memory → cold boot, no crash.
4. Test: boot counter increments and wraps at 8 (3 bits).
5. Test: cold-boot and soft-reset flags are set correctly and independently.

## Done when

- [ ] Version mismatch forces a cold boot.
- [ ] Garbage memory cannot produce a false restore.
- [ ] Flag semantics pinned.
