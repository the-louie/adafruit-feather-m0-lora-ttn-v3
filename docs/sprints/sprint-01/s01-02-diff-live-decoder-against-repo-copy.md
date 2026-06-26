# S01-02 — Diff live decoder against repo copy

**Estimate:** 1–2 h
**Backlog item:** TODO #13
**Depends on:** S01-01
**Needs hardware:** no

## Context

`ttn-decoder-v6.js` is a stale artifact of unknown provenance; the live decoder is strictly more capable. Before deleting or promoting either, understand the divergence — the live one contains logic the repo copy never had, the timestamp extrapolation especially, which is the actual consumer of byte 0.

**Versioning context** (settled 2026-07-17): v5 = 8-byte legacy, **v6 = current** (9-byte, wake counter), v7 = new work (9-byte uplink counter, and 15-byte solar). See `docs/solar-variant-design.md` § Protocol versioning.

## Steps

1. Diff the two files.
2. For each divergence, record which behaviour is intended:
   - `version` field: hardcoded 5 (live) vs hardcoded 6 (repo). **Both are wrong** — it should be derived. The live decoder reports `version: 5` for `gisebo-01`'s 9-byte **v6** payloads, which caused a false diagnosis during planning that TTN was misdecoding v6 as v5. Fix in S02-06.
   - `entries` + per-sample timestamps (live) vs flat `temperatures` (repo).
   - null slots omitted (live) vs `null` pushed (repo).
   - 8-byte support (live) vs 9-byte-only (repo).
3. Establish where the live decoder gets its interval for **8-byte** payloads, which have no interval byte. That assumption is load-bearing for `gisebo-04`'s entire history.
4. Note whether the live decoder is set per-application or per-device. Per-device matters: S02-06 introduces a `FIRMWARE_VERSION` constant that must be pinned per unit, and it only works if decoders really are per-device.

## Done when

- [ ] Divergence table written into a dev-note.
- [ ] Decision recorded for each difference.
- [ ] The hardcoded-`version` bug recorded and handed to S02-06.
- [ ] The 8-byte extrapolation assumption documented.
- [ ] Per-device vs per-application decoder configuration confirmed.
