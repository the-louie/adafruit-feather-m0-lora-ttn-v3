# S08-01 — Publish the v7 output schema as a handover artifact

**Estimate:** 2 h
**Backlog item:** — (cutover; see `docs/sprints/sprint-08/README.md`)
**Depends on:** sprint-04
**Needs hardware:** no

## Context

Everything downstream — telegraf, influx, grafana — needs to know exactly what a v7 uplink decodes to. Right now that knowledge lives in a decoder nobody outside this repo reads.

## Steps

1. Write the decoded-JSON shape for **both** v7 payloads: 9-byte primary (FPorts 10/20) and 15-byte solar (11/21).
2. Include the field-level diff against gisebo-01's current output — that diff *is* the migration:
   - `sequence` → `uplink_counter` (renamed **and** re-meaning: +1 per uplink rather than the wake counter's {1,7,13})
   - `version` 5 → 7, derived rather than hardcoded
   - added: panel voltage, panel current, sun EWMA, harvest accumulator, status flags
   - `entries[]` keeps its shape — timestamps stay byte-position-derived
3. Include worked examples with real hex, taken from the S05 vectors so the schema and the tests cannot drift.
4. Commit it where the telegraf owner will find it, not only in this repo.

## Done when

- [ ] Both v7 shapes documented with worked examples.
- [ ] The field-level diff against gisebo-01's output is explicit.
- [ ] Handed to whoever owns telegraf.
