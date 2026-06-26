# S07-13 — Dev-note solar bring-up findings

**Estimate:** 2 h
**Backlog item:** TODO #8
**Depends on:** S07-12
**Needs hardware:** YES

## Context

The whole point of bring-up is finding where the design was wrong. Write those down, especially the contradictions.

## Steps

1. One dev-note per surprise. Assumptions that held get a line; assumptions that broke get a full note.
2. Update `docs/solar-variant-design.md` with measured numbers replacing every estimate — harvest, per-wake energy, real Vmp, the day/night threshold.
3. Retrospective: what did shipping sprints 02–04 on compile-only verification actually cost? That answer should inform how the next project is sequenced.

## Done when

- [ ] Findings documented, contradictions especially.
- [ ] Design doc carries measurements, not estimates.
- [ ] The compile-only-verification cost is assessed honestly.
