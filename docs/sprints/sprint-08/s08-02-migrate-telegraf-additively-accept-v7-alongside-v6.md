# S08-02 — Migrate telegraf additively — accept v7 alongside v6

**Estimate:** 3 h
**Backlog item:** — (cutover; see `docs/sprints/sprint-08/README.md`)
**Depends on:** S08-01
**Needs hardware:** no

## Context

**Additive, not a replacement**, and the real `telegraf.conf` makes it nearly free: it is `json_v2` with `optional = true` on **every** field, so adding fields cannot break v6 — a missing field is skipped, never an error.

What already works for v7 with no change at all: `battery_v` and `entries[]` keep the same JSON paths, so they land today. What is missing: `uplink_counter`, `interval_index`, and every solar field.

**`interval_index` is the important one, and it is missing for v6 too** — see below.

## Steps

1. Add the v7 fields as `optional = true`, mirroring the existing style:
   - `uplink_counter` (int) — a **new field**, not an alias of `sequence`. They mean different things; conflating them would make the historical series lie.
   - `interval_index` (int) and `interval_minutes` (int) — see below.
   - `panel_v` (float), `panel_ma` (float), `sun_ewma` (float), `harvest_mah` (int), `boot_counter` (int), status flags (bool).
2. Keep v6/FPort 10 handling exactly as-is. **Do not refactor it.** gisebo-01 depends on it and it retires in a fortnight; churning it now buys nothing and risks the live site.
3. Handle `uplink_counter` as a distinct field from `sequence` rather than aliasing — they mean different things, and conflating them would make the historical series lie.
4. Confirm the discard path still discards: a malformed or unknown-FPort uplink must not error the pipeline.

## Done when

- [ ] v7 fields land in influx, including `interval_index`.
- [ ] gisebo-01's v6 path untouched and still working.
- [ ] `uplink_counter` is its own field, not aliased onto `sequence`.
- [ ] Unknown shapes still discard silently rather than erroring.

## The gap this exposed: byte 0 has never reached the backend

`interval_index` is **not emitted by any live decoder and not mapped in telegraf**. Byte 0 — the single output of the entire temperature/battery interval algorithm, the thing three sprints of work exist to compute — has never been visible to the backend at all.

The decoders *consume* it (gisebo-01's uses it to space `entries[]` timestamps) but never *report* it. So nobody can see what the interval policy decided, cross-check it against the sample spacing, or alarm when a unit parks at index 10 — which is exactly the silent-decommission symptom S01-09 exists to catch.

This flatly contradicts the design's own premise that uplinks are the only instrument. Emit `interval_index` and `interval_minutes` from the v7 decoder and map both here.
