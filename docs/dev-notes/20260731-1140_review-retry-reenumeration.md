# Review remediation: retry every failed flavour, re-enumerate on any failure, refresh ROM on success

Date: 2026-07-31 11:40
Source: `__doc/code_reviews/20260731-1132_sensor-diagnostics-review.md` (review of 2c93b5a..f1935b3, the items 27-31 queue plus the build script).

## Summary

Three defects in the freshly implemented sensor diagnostics, all of the same
shape: the diagnostics failed to report truthfully in exactly the scenarios
they were built for.

1. **Stuck-85 was never retried.** The retry trigger (`NaN || <= -100`)
   covered the disconnect classes but excluded `85.0`, yet stuck-85 means "the
   conversion never ran" — precisely the transient class (brown-out, hot-plug
   mid-conversion) the in-wake retry exists to absorb. The trigger is now
   `ds18DeriveStatus(...) != DS18_OK`, the same host-tested derivation that
   later produces the status byte, so every failed flavour gets its one retry.
2. **The ROM identity went stale across a live swap.** `getTempCByIndex()`
   re-searches the bus on every call, so a sensor swapped between wakes reads
   fine while the fault frame kept reporting the setup()-time serial — item 31
   defeated in its primary scenario. A successful read now refreshes the ROM
   (`ds18CaptureRom()`); on failure the last-known serial is kept deliberately
   so a `not_found` frame still names which sensor was lost.
3. **Topology faults fossilized.** Re-enumeration ran only at `count == 0`, so
   a chain that failed open after boot kept reporting `crc_or_no_response`
   (cached count 1) instead of the truthful `not_found` — the exact signature
   that identified gisebo-01 — and a 2-sensor bus reduced to 1 stayed
   `AMBIGUOUS` until reboot. Any failed read now re-enumerates first, so the
   status byte describes the bus as it is NOW.

Mechanical companions: the duplicated PROD/DEV conversion wait was extracted
into `ds18ConversionWait()` (the idle(750) history comment moved with it), and
the triplicated ROM capture into `ds18CaptureRom()`.

## Costs

All new work lands only in the failure path: one bus search plus one extra
conversion window per failed wake. A healthy wake pays nothing. The radio is
idle throughout (reads precede any TX), so no MAC-timing rule is touched.

## Verification

- Host suite: all pass (366 assertions; the retry trigger is now the
  already-tested `ds18DeriveStatus`, no new pure logic was added).
- Decoder suites: 17 + 69 pass, unchanged wire formats.
- `arduino-cli compile` clean, 75232 B (28%).
- On-device: falls under the items 27-31 flash-verify criteria already in
  TODO.md (next flash of gisebo-05).
