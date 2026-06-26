# Sprint 05 — Test vectors, backend, documentation

**Dates:** 2026-09-10 (Thu) → 2026-09-23 (Wed)
**Capacity:** 1 developer, ~30 h effective
**Planned:** 20 tasks, ~33 h — gained clarity ratio and out-of-contract handling from sprint 04, where they blocked nothing

## Goal

Close the loop. Replace the dead test vectors, build the alarms that make silent failures visible, and bring the documentation in line with what was actually built.

## Why this is not filler

Uplinks are the only instrument this project has — no bench, no test devices. That makes the backend tasks (09–12) genuinely load-bearing rather than nice-to-have: they are the entire observability story for a fleet on posts at a lake.

And the vectors are what stop the next `rebootDetected`. That field survived for the life of the project because `doc/test-payloads.md` asserted behaviour nobody ever executed. **Every vector here must run in the harness.** A vector that is only read is worse than no vector, because it looks like coverage.

## Working agreement

- **Commit in small batches**: one function + its test (if available) + its documentation, per commit. Several commits per task; never one commit per task.
- Every non-trivial change gets a dated dev-note in `docs/dev-notes/`.

## Task index

| # | Task | Est | Item |
|---|---|---|---|
| 01 | Move test-payloads into docs and update references | 1 h | 3 |
| 02 | Primary vectors cold start healthy summer winter | 2 h | 3 |
| 03 | Primary vectors uplink counter continuity and gaps | 1–2 h | 3 |
| 04 | Solar vector full sun full pack | 2 h | 3 |
| 05 | Solar vectors night overcast and surplus | 2 h | 3 |
| 06 | Solar vectors cold boot versus soft reset | 1–2 h | 3 |
| 07 | Sentinel and encoder limit vectors | 2 h | 3 |
| 08 | Wire all vectors into the harness with coverage | 2 h | 3 |
| 09 | Backend alarm clock never acquired | 1–2 h | 10 |
| 10 | Backend pack health trend | 2 h | 10 |
| 11 | Backend panel fouling alarm | 2 h | 10 |
| 12 | Backend retroactive lag correction | 2 h | 1 |
| 13 | Update the design doc to as-built | 2 h | — |
| 14 | Update cursor skills for the solar variant | 2 h | — |
| 15 | Update CLAUDE.md for the three-way split | 2 h | — |
| 16 | Fleet reflash execution plan | 1–2 h | 14 |
| 17 | Release and migration notes | 1–2 h | — |
| 18 | Sprint 06 readiness review | 1 h | 12 |
| 19 | Decoder clarity ratio | 2 h | 9 |
| 20 | Decide out-of-contract temperature byte handling | 2 h | 3 |

## Exit criteria

- Every decoder branch is covered by an executable vector.
- Every silent failure mode identified in this project has an alarm.
- Docs describe what was built, not what was planned.
- Sprint 06 is either datable (hardware ordered) or formally parked.
