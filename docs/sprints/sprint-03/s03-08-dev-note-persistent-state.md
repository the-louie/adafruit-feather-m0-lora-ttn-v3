# S03-08 — Dev-note persistent state

**Estimate:** 1 h
**Backlog item:** TODO #5
**Depends on:** S03-07
**Needs hardware:** no

## Context

Convention: one dated dev-note per change.

## Steps

1. Explain why `.noinit` is consistent with the no-FlashStorage rule — SRAM survives a soft reset; only the C runtime clears it.
2. Record the version-bump rule and why it exists.
3. Record what is stored and why each field needs to survive.

## Done when

- [ ] Dev-note committed.
- [ ] The no-FlashStorage consistency argument is explicit, since it looks like a violation at first glance.
