# S08-05 — Annotate the one-interval lag and the cutover boundary

**Estimate:** 2 h
**Backlog item:** — (cutover; see `docs/sprints/sprint-08/README.md`)
**Depends on:** S08-04
**Needs hardware:** no

## Context

**gisebo-01's entire history is one interval late** — the `idle(750)` defect means every reading it has ever produced is timestamped 30 minutes after the water it actually measured. gisebo-05 will not be. Same site, same panel, a systematic discontinuity at the swap.

Decided 2026-07-17: **annotate, do not correct.** Shifting a real historical series risks corrupting it, and TTN only retains ~3 days, so most of the history is already in influx and would have to be rewritten in place.

## Steps

1. Add a grafana annotation at the cutover moment.
2. Add a standing annotation or panel note covering gisebo-01's whole span: **samples are one interval (30 min) later than the water they measured**, because of the `idle(750)` defect.
3. Record it in the handover doc too — anyone correlating this series against weather, another sensor, or gisebo-05 needs to know before they draw a conclusion.
4. Do **not** rewrite influx.

## Done when

- [ ] Cutover annotated.
- [ ] The lag documented against gisebo-01's whole history.
- [ ] No historical data mutated.
