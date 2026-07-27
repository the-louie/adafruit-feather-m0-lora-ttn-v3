# Verbose DEV diagnostics frame (FPort 3) — TODO item 16

**Date:** 2026-07-27 ~18:04 UTC
**Scope:** New DEV-only telemetry frame. New protocol surface (FPort 3); data and
fault frames unchanged.

## Why

The fault frame (`diagnostics.h`, FPort 1/2) only speaks up on faults. During
bench/DEV bring-up we also want the **full** live state on a steady cadence to
confirm everything looks OK — battery, panel V/I, sun EWMA, harvest, season/band,
interval, sensor, INA219 config — not just "no faults". Requested for a planned
power outage: flash gisebo-05 DEV and collect hourly full-state snapshots through
the outage for analysis afterward.

## What

- **FPort 3, DEV-only.** The entire path is gated on `runMode == 1`; PROD never
  emits it. Verified in host tests (`verboseShouldSend(isDev=false, …)` is always
  false).
- **Cadence: once on the first operational cycle after join, then every
  `VERBOSE_INTERVAL_MS` (default 3 600 000 = ~1 h).** DEV never deep-sleeps, so
  `millis()` advances and gates the cadence directly — no clock or `.noinit`
  dependency. The gate uses unsigned subtraction, so it is correct across a
  `millis()` wrap (tested). Battery is deliberately not a concern in DEV, so it is
  unconditional (no fault/rate gating). **For a short outage, lower
  `VERBOSE_INTERVAL_MS` for finer resolution** — it is a one-line `#define`.
- **22-byte payload** (schema 1): info bits (solar/dev/cold_boot/clock_valid/
  ina219_seen/bonus_active/bus_ambiguous), reset cause, boot counter, interval
  index, season|band, battery mV, panel bus mV, panel current (0.1 mA/LSB), sun
  EWMA, harvest mAh, INA219 config, DS18B20 count, surface temp (centi-°C, 0x7FFF
  = invalid), and the full fault bitmap (so "all-clear" = 0x0000 is explicit).
  Byte map in `TODO.md` item 16 and mirrored by the decoder's `decodeVerbose()`.

## Implementation

- `diagnostics.h`: `struct VerboseSnapshot`, `diagEncodeVerbose()`,
  `verboseShouldSend()` — pure, host-tested (`test/host/test_diagnostics.cpp`,
  incl. the DEV gate, the wrap case, and every byte's placement).
- `.ino`: extracted `txFrameAndWait(fport, payload, len)` (shared by the fault and
  verbose frames — the `OP_TXRXPEND` guard + bounded TXCOMPLETE wait); added
  `gatherVerbose()` (reads `solarPolicy`/`primaryPolicy`, `currentIntervalIndex`,
  `surfaceTempC`, the `g_*` diag inputs) and `evaluateAndMaybeSendVerbose()`,
  called once per operational cycle after `evaluateAndMaybeSendDiag()`.
  Out-of-band: never touches `currentIntervalIndex` or the uplink counter.
- `decoders/gisebo-05-v7.js`: `decodeVerbose()` for FPort 3, dispatched before the
  data-FPort checks; crafted vector in `test/run.js`.

FPort map: data 10/20 (primary) · 11/21 (solar); faults 1 (PROD) / 2 (DEV);
**verbose 3 (DEV)**.

## Verification

- Host tests green (DEV gate, wrap, byte map, temp sentinel).
- Decoder: `node test/run.js` → 17 passed (verbose full-state vector + wrong-length).
- Compiles: **72844 bytes (27%)**.
- On hardware: pending the flash. Expect a DEV unit to emit one FPort-3 frame at
  boot then ~hourly; a PROD unit emits none. Pairs with TODO item 15 (this is the
  over-the-air readout for battery/solar metering verification).
