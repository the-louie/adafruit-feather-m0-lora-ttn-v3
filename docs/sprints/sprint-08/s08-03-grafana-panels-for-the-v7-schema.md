# S08-03 — Grafana panels for the v7 schema

**Estimate:** 2 h
**Backlog item:** — (cutover; see `docs/sprints/sprint-08/README.md`)
**Depends on:** S08-02
**Needs hardware:** no

## Context

The site's dashboards query gisebo-01's fields. After the swap those fields stop arriving.

## Steps

1. Update the site's panels to read the v7 fields.
2. Add the solar panels that did not exist before: panel V/I, harvest accumulator, sun EWMA, and the derived clarity ratio.
3. Keep the v6 queries alive until S08-11 — during rehearsal both devices report and both must render.
4. Add a **staleness alarm on the site itself**: if no uplink arrives for N intervals, alert. Today a silent discard produces a flat panel and no notification; that is the exact failure mode the cutover risks.

## Done when

- [ ] Panels render v7 data.
- [ ] Solar panels added.
- [ ] A site-level staleness alarm exists and has been seen to fire.
