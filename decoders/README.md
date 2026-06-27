# Decoders

Exported verbatim from the TTN console 2026-07-17 via the Application Server API
(`GET /as/applications/telamon-temperature/devices/{dev}?field_mask=formatters`).
**Do not edit these to fix things** — they are the record of what production ran
on that date. Changes belong in a new file, deployed deliberately.

| file | device | payload | notes |
|---|---|---|---|
| `live-gisebo-01-9byte.js` | gisebo-01 | 9-byte (interval byte present) | reports `version: 5` on a v6 payload |
| `live-gisebo-04-8byte.js` | gisebo-04 | 8-byte (no interval byte) | hardcodes a 5-minute interval |

**There is no application-level default formatter.** Formatters are set
**per device**, and the two devices run *different* decoders. The per-device
model the v7 plan assumes is therefore not a new idea — it is already how this
runs, which is why a per-device `FIRMWARE_VERSION` constant fits naturally.

## What the export established

- **`version` is hardcoded**, not derived. gisebo-01 reports `version: 5` for a
  9-byte v6 payload; gisebo-04 reports `5.0` (a float, for no reason). This is
  what caused a false diagnosis during planning that TTN was misdecoding v6.
- **Both interval assumptions are correct.** gisebo-01's byte 0 reads index 4
  (30 min) and its real median inter-uplink gap is 180.2 min = 6 x 30.03 min.
  gisebo-04 has no interval byte, hardcodes 300 s, and its real gap is 30.2 min
  = 6 x 5.03 min. An earlier concern that gisebo-04's timestamps were 12x wrong
  was unfounded.
- **`rebootDetected` is not in production at all.** It exists only in the repo's
  stale `ttn-decoder-v6.js`. The underlying defect is real — sequence never
  reaches 0 — but nothing was ever misled by it, because the field was never
  deployed.
- **Timestamps cannot misalign.** Both decoders compute `sensorIdx` from the
  *byte position* (`i - 3` / `i - 2`), so a skipped value does not shift the
  others. An earlier claim that dropped bytes would misalign every later reading
  applied to the repo file's positional array, not to these.

## Real defects in the live decoders

- **Out-of-contract temperature bytes produce phantom entries.** The chain is
  `250 -> continue`, `251`, `252`, `v <= 200`. Bytes **201-249 and 253-255**
  match nothing, so an entry is pushed carrying a `timestamp` and **no
  temperature and no state** — a datapoint that exists and says nothing. Not
  reachable from current firmware; decide deliberately in S05-20.
- **gisebo-01 accepts `length < 3`.** A truncated 3-byte payload decodes to
  garbage battery and no entries rather than erroring.
