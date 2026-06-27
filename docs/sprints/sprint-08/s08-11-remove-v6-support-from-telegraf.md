# S08-11 — Remove v6 support from telegraf

**Estimate:** 2 h
**Backlog item:** — (cutover; see `docs/sprints/sprint-08/README.md`)
**Depends on:** S08-10
**Needs hardware:** no

## Context

Housekeeping, deliberately last. Once gisebo-01 is retired and nobody wants it back, v6 handling is dead code in a live pipeline.

## Steps

1. Confirm no device sends v6 (gisebo-04 sends **v5** on FPort 10 and is untouched — check whether it flows through this webhook before removing anything).
2. Remove v6 handling.
3. **Do not** remove v5 handling if gisebo-04 still reports through it.

## Done when

- [ ] v6 path removed.
- [ ] gisebo-04's v5 path confirmed unaffected.
