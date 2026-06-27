# S08-04 — Rehearse end-to-end from the bench, gisebo-01 still live

**Estimate:** 3 h
**Backlog item:** — (cutover; see `docs/sprints/sprint-08/README.md`)
**Depends on:** S08-03
**Needs hardware:** YES

## Context

**The task that makes the swap safe.** gisebo-05 sits on a bench transmitting real v7 uplinks through the real gateway, the real webhook, the real telegraf, into the real influx, onto the real dashboards — while gisebo-01 continues serving the site as the reference.

Everything except the lake is tested before anything is irreversible.

## Steps

1. Join gisebo-05 (solar-strapped, INA219 fitted) from the bench.
2. Confirm its uplinks arrive on FPort 11, decode via its own formatter, pass telegraf, and appear in grafana.
3. Confirm gisebo-01 is **completely unaffected** throughout — same cadence, same fields, same panels.
4. Let it run long enough to see an interval change and at least one full batch cycle, so the timestamp extrapolation is exercised rather than assumed.
5. Compare bench water temperature against gisebo-01's lake reading only for sanity — they measure different water, so agreement is not expected. What matters is that the *shape* is right.

## Done when

- [ ] gisebo-05 renders in grafana from the bench.
- [ ] gisebo-01 demonstrably unaffected.
- [ ] Timestamp extrapolation verified across an interval change.
