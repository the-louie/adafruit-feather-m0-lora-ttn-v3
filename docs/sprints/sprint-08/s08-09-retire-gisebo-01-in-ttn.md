# S08-09 — Retire gisebo-01 in TTN

**Estimate:** 1 h
**Backlog item:** — (cutover; see `docs/sprints/sprint-08/README.md`)
**Depends on:** S08-08
**Needs hardware:** no

## Context

Only after gisebo-05 has held the site for a week. Until then gisebo-01 is the rollback.

## Steps

1. Confirm S08-08 passed and nobody wants to roll back.
2. Retire the device in TTN. **Do not delete the decoder from `decoders/`** — it is the frozen record of what produced years of data, and anyone reading that history needs it.
3. Record the retirement date beside the cutover annotation.

## Done when

- [ ] gisebo-01 retired only after a clean week.
- [ ] Its decoder preserved in `decoders/` as a record.
