# S07-12 — Field deployment readiness review

**Estimate:** 2 h
**Backlog item:** TODO #14
**Depends on:** S07-11, S05-16
**Needs hardware:** YES

## Context

The fleet is two units, one of which runs firmware whose source may not exist. This decides whether the reflash goes ahead.

## Steps

1. Confirm every S06 and S07 exit criterion is met, or explicitly waived by someone who understands the waiver.
2. Confirm the reflash plan (S05-16) still holds after everything verification changed.
3. **Confirm rollback is real.** These are post-mounted at a lake; if a unit goes silent after reflash, the answer may be a site visit, and that should be known before rather than after.
4. Confirm the backend alarms all fire — probe misdetect, 252 signature, clock-never-acquired, panel fouling. Alarms nobody has watched fire are decoration.

## Done when

- [ ] All exit criteria met or explicitly waived.
- [ ] Reflash plan still valid post-verification.
- [ ] Rollback confirmed realistic.
- [ ] Every alarm proven to fire.
