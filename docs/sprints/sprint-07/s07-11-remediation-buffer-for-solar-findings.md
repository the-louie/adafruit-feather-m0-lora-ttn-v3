# S07-11 — Remediation buffer for solar findings

**Estimate:** 4 h
**Backlog item:** TODO #12
**Depends on:** S07-10
**Needs hardware:** YES

## Context

As with S06-10, this is not padding. The solar policy has never run on hardware and several of its assumptions — the load-side bus voltage, the day/night threshold, the charge-terminated behaviour, the non-hunting EWMA — are arguments rather than facts until this sprint.

## Steps

1. Triage findings from tasks 01–10.
2. Fix in priority order: wrong data first, silent failure second, inefficiency third.
3. Anything not fixed goes to TODO.md rather than being dropped.

## Done when

- [ ] Findings triaged and fixed.
- [ ] Unfixed items recorded.
