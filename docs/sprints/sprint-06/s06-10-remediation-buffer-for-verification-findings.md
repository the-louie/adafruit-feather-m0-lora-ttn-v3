# S06-10 — Remediation buffer for verification findings

**Estimate:** 4 h
**Backlog item:** TODO #12
**Depends on:** S06-07
**Needs hardware:** YES

## Context

**This is not padding.** Sprints 02–04 ship on compile-only verification; the realistic expectation is that this sprint finds something. Budgeting zero time to fix what verification finds would mean either the findings get ignored or the sprint overruns.

Most likely candidates, in rough order: the RTC ownership seam (S06-03), the `.noinit` false-restore-after-brief-power-interruption case (S06-06), and `DeviceTimeReq` not landing through the real gateway (S06-07).

## Steps

1. Triage what tasks 02–07 found.
2. Fix in priority order: anything that corrupts data first, anything that hangs second, anything merely wrong third.
3. Keep to the commit policy — a fix is still one function + test + doc.
4. If findings exceed the buffer, that is a signal worth escalating rather than absorbing quietly.

## Done when

- [ ] Findings triaged and prioritised.
- [ ] Data-corrupting issues fixed.
- [ ] Anything left unfixed is recorded in TODO.md, not dropped.
