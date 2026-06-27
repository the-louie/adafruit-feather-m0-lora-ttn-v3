# Sprint 08 — Cutover: gisebo-05 replaces gisebo-01

**Dates:** 2026-10-22 (Thu) → 2026-11-04 (Wed) — **provisional, gated on sprints 06–07**
**Capacity:** 1 developer, ~30 h effective
**Planned:** 12 tasks, ~25 h

## Goal

Replace gisebo-01 with gisebo-05 at the production site, and migrate telegraf/influx/grafana to the v7 schema in the same operation. gisebo-01 is retired; the two are never in production together.

## The cutover can be made almost risk-free, and the reason is counter-intuitive

**The webhook recipient discards data that does not fit.** During development that is a gift: gisebo-05 can sit on a bench transmitting v7 into the live pipeline for weeks and be silently dropped, disturbing nothing.

It is also exactly what makes the swap dangerous — **if the pipeline is not migrated, the site goes dark in grafana silently, because telegraf drops rather than errors.** Nobody gets an alert. A dashboard just stops moving.

So the sequencing inverts the usual risk. Do **not** swap and then migrate:

1. **Migrate telegraf to accept v7 *additively*, while gisebo-01 is still live.** v6 keeps working; nothing breaks.
2. **Verify gisebo-05's bench data lands correctly in influx and grafana** — with gisebo-01 still running as the reference. This is the real test, and it happens before anything irreversible.
3. **Only then swap physically.** By that point the only untested thing is the lake.

The single irreversible moment is the site visit. Everything else is rehearsed first.

## What changes at the wire

gisebo-05 is **solar**, not a like-for-like replacement — the site's power architecture changes too:

| | gisebo-01 (today) | gisebo-05 (after) |
|---|---|---|
| FPort | 10 | **11** |
| payload | 9 bytes | **15 bytes** |
| power | 6 V primary pack | **1S2P li-ion + panel** |
| `sequence` | wake counter, values {1,7,13} | **`uplink_counter`**, +1 per uplink |
| `version` | hardcoded 5 (on a v6 payload) | **derived, 7** |
| new fields | — | panel V/I, sun EWMA, harvest, status |

## Working agreement

- **Commit in small batches**: one function + its test (if available) + its documentation, per commit.
- Every non-trivial change gets a dated dev-note in `docs/dev-notes/`.

## Task index

| # | Task | Est |
|---|---|---|
| 01 | Publish the v7 output schema as a handover artifact | 2 h |
| 02 | Migrate telegraf additively — accept v7 alongside v6 | 3 h |
| 03 | Grafana panels for the v7 schema | 2 h |
| 04 | Rehearse end-to-end from the bench, gisebo-01 still live | 3 h |
| 05 | Annotate the one-interval lag and the cutover boundary | 2 h |
| 06 | Site-visit plan and rollback | 2 h |
| 07 | Physical swap | 2 h |
| 08 | Post-swap verification | 2 h |
| 09 | Retire gisebo-01 in TTN | 1 h |
| 10 | Re-point the FPort 10 misdetect alarm | 1 h |
| 11 | Remove v6 support from telegraf | 2 h |
| 12 | Dev-note the cutover | 2 h |

## Exit criteria

- The site reports continuously in grafana across the swap, with the boundary annotated.
- gisebo-01 retired; its decoder kept as a frozen record in `decoders/`.
- No silent gap: an alarm fires if the site stops reporting, rather than a panel quietly flatlining.
