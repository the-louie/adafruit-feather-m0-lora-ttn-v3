# S04-15 — Decoder parse the solar fields

**Estimate:** 2 h
**Backlog item:** TODO #9
**Depends on:** S04-14
**Needs hardware:** no

## Context

Panel V, panel I, EWMA, harvest, status. These are the maintenance signals — pack health and panel fouling both come from here, and there is no other way to see them.

## Steps

1. Decode each field with its scaling.
2. Expand the status byte into named booleans, not a number. `cold_boot: true` is usable; `status: 19` is not.
3. Surface the boot counter separately.

## Done when

- [ ] All fields decoded with correct scaling.
- [ ] Status flags expand to named booleans.
- [ ] Vectors added to the harness.
