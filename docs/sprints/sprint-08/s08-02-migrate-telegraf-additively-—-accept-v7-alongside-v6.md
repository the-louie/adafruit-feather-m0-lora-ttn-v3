# S08-02 — Migrate telegraf additively — accept v7 alongside v6

**Estimate:** 3 h
**Backlog item:** — (cutover; see `docs/sprints/sprint-08/README.md`)
**Depends on:** S08-01
**Needs hardware:** no

## Context

**Additive, not a replacement.** gisebo-01 is still live and must keep working; v7 support is added beside v6 rather than swapped in. That is what lets the cutover be rehearsed instead of attempted.

The recipient discards what does not fit, so today gisebo-05's bench uplinks are dropped. After this task they land — while gisebo-01 continues untouched.

## Steps

1. Add v7 handling: FPort 11 (and 21 for DEV), 15-byte payload, the new fields.
2. Keep v6/FPort 10 handling exactly as-is. **Do not refactor it.** gisebo-01 depends on it and it retires in a fortnight; churning it now buys nothing and risks the live site.
3. Handle `uplink_counter` as a distinct field from `sequence` rather than aliasing — they mean different things, and conflating them would make the historical series lie.
4. Confirm the discard path still discards: a malformed or unknown-FPort uplink must not error the pipeline.

## Done when

- [ ] v7 uplinks land in influx.
- [ ] gisebo-01's v6 path is byte-for-byte unchanged and still working.
- [ ] Unknown shapes still discard silently rather than erroring.
