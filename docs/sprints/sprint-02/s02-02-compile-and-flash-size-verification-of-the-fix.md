# S02-02 — Compile and flash-size verification of the fix

**Estimate:** 1 h
**Backlog item:** TODO #1
**Depends on:** S02-01
**Needs hardware:** no

## Context

No devices exist, so compilation is one of only two verification tools available. Record the baseline now — it is also how we will notice the refactor bloating things later.

## Steps

1. `arduino-cli compile` for Adafruit Feather M0 with `CFG_eu868`.
2. Record flash and RAM usage as the baseline for the sprint.
3. Confirm no new warnings.

## Done when

- [ ] Clean compile.
- [ ] Flash/RAM baseline recorded in the dev-note.
