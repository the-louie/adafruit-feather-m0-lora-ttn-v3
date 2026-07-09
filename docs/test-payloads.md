# Test payloads (protocol v7)

**These vectors are executable.** They live in `test/run_v7.js` and run under `npm test`. This document is the human-readable index; the runner is the source of truth. The previous version of this file documented 8-byte V5 vectors that failed the decoder's length check and validated nothing — which is exactly what happens when vectors are hand-maintained and never run. Do not reintroduce a vector here that the harness does not execute.

Decoder under test: `decoders/gisebo-05-v7.js`.

## Primary variant (9 bytes, FPorts 10/20)

| # | hex | decodes to |
|---|---|---|
| P1 | `04 AD 55 86 86 86 86 86 86` | interval 4 (30 min), battery 5.768 V, uplink_counter 5, 6× 16.8 °C |

Bytes 0–8 are identical to the solar variant, so the temperature/battery output matches gisebo-01's schema and telegraf keeps working across the cutover.

## Solar variant (15 bytes, FPorts 11/21)

| # | hex | decodes to |
|---|---|---|
| S1 | `02 38 43 8C..8C 9C 32 99 01 2C 4C` | interval 2 (5 min, floor), 3.900 V, counter 3, 6× 18.0 °C, panel 4.8 V / 25 mA, EWMA 0.6, harvest 300 mAh, boot 2, clock-valid + bonus-active |
| S2 | *charge-terminated* | 0 mA at panel Voc (6.0 V) → reads as **sun**, EWMA ~0.9, clarity > 0. **The most important vector in the file** — proves the signal keys on voltage, not current. |
| S3 | *clock invalid* | status has no clock-valid bit → `clarity: null` (a ratio from an unseeded clock is nonsense) |

## Edge and error vectors

| # | case | expected |
|---|---|---|
| E1 | 15 bytes on FPort 10 | error (length/FPort mismatch), not garbage |
| E2 | 9 bytes on FPort 11 | error |
| E3 | unknown FPort | error |
| E4 | temperature byte 220 (201–249, out of contract) | reported as `temperature_state: "out of range", raw: 220`, **not** silently dropped (S05-20), and later slots keep their positions |

## Coverage

The runner exercises: both payload lengths, all four FPorts, `interval_index` 0/>10 clamping, the three temperature sentinels (250/251/252), the out-of-contract range, both battery clamps, `uplink_counter` (not `sequence`), the derived version field, every status flag, the charge-terminated case, and clarity present/null.

## What is deliberately NOT here yet

- **Real captured v7 bytes.** gisebo-05 does not exist yet, so every vector is built from the firmware's own field encodings. When gisebo-05 transmits, add real captured uplinks the way `test/fixtures-live.json` does for gisebo-01/04 — production bytes with TTN's own decoded output as the expected value.
- **Vector diversity note (from S01-04 QA):** a battery-offset bug was once caught by only 1 of 2 vectors because `bytes[2] < 32` masked it. Prefer varied byte values over more vectors of the same shape.
