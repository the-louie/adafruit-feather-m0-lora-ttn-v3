# S06-11 — Re-verify after remediation

**Estimate:** 2 h
**Backlog item:** TODO #12
**Depends on:** S06-10
**Needs hardware:** YES

## Context

A fix written against a bench unit and not re-run on it is just a compile-only fix again.

## Steps

1. Re-run every check that failed in tasks 02–07.
2. Confirm the fixes did not break something that previously passed — particularly sleep timing, which everything depends on.
3. Run a multi-hour soak: many wake/sleep/TX cycles, confirm timing stays accurate and nothing wedges.

## Done when

- [ ] All previously failing checks pass.
- [ ] No regressions in previously passing checks.
- [ ] Multi-hour soak clean.
