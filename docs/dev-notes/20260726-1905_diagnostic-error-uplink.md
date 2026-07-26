# Field-observable diagnostics: an error/health uplink on its own FPort

**Date:** 2026-07-26 ~19:05 UTC
**Scope:** New feature. `diagnostics.h` (+ host tests), decoder branch, `.ino` glue,
`persist.h` rate-limit state. New protocol surface (FPort 1/2); data payloads
(FPorts 10/11/20/21) unchanged.

## Why

USB/serial is unavailable on a deployed unit, and the data payload carries almost
no health signal: the core 9-byte payload has **no status byte at all** (only the
solar variant's appended byte 14 does), so a primary-cell board is completely
blind, and even the solar status is coarse. A dead DS18B20, a flaky I2C bus, a
decayed-RAM restore, or a dying pack are today either invisible or only inferable
indirectly (null temp slots, wrong FPort). This adds a dedicated diagnostic uplink
so those faults are directly observable in the field.

Requested by the user; trigger policy chosen as **Option A** (boot + rate-limited
fault) in discussion.

## Design

- **Separate FPort, variant-independent:** `1` (PROD) / `2` (DEV). Keeps the data
  ports {10,11,20,21} and the A1 misdetect alarm untouched.
- **11-byte frame** (`diagnostics.h`): schema, info bits (variant / mode / cold
  boot / clock valid / INA219 seen), reset cause (`PM->RCAUSE`), boot counter,
  OneWire device count, fault bitmap, INA219 probe config register, battery mV.
- **Fault bitmap** (actionable only): DS18B20 not found / bus ambiguous / read
  fail; INA219 read fail (solar); persist corrupt (decayed RAM, distinct from a
  normal cold boot via `persistDecayedButFramed()`); last-cycle TX timeout; low
  battery (hard 3400 mV floor common to both packs). **"INA219 missing" is
  deliberately NOT a fault** — one binary serves every board, so a primary unit
  legitimately finds none; a mis-probed solar board shows up as the wrong data
  FPort (the existing A1 alarm) instead.
- **Send policy** (`diagShouldSend`): one frame per boot (after the first read, so
  sensor/probe inputs are populated); a **new distinct fault** is reported
  promptly; a **persistent fault** re-alerts at most once per `minResend`
  (24 h). Spam-proof with or without a clock — before the clock lands, only rising
  edges fire, each fault exactly once. The latch (`diagLastSentFaults` /
  `diagLastSentEpoch`) lives in the CRC-protected persist struct
  (`PERSIST_VERSION` 1→2), so it survives soft resets.
- **Out-of-band:** the diagnostic is sent after the data uplink and never touches
  `currentIntervalIndex` or the uplink counter, so it cannot perturb telemetry
  cadence or the two-write-points interval rule.

## Split of judgement vs glue (per the repo rule)

`diagnostics.h` holds all the judgement (fault computation, encoding, send/rate
decision) and is host-tested in `test/host/test_diagnostics.cpp`. The `.ino` only
gathers Arduino-side inputs and transmits. The decoder mirrors the header and is
tested in `test/run.js` with crafted vectors computed from the same spec.

## Verification

- Host tests green (incl. new `test_diagnostics` and extended `test_persist`).
- Decoder tests green: `node test/run.js` → 13 passed (3 new diagnostic vectors:
  healthy boot, fault frame, wrong length).
- Compiles: **72020 bytes (27%)** (`arduino-cli compile --fqbn
  adafruit:samd:adafruit_feather_m0 .`).
- On-device: pending the flash of gisebo-05. Expected first-boot behaviour: a data
  flush on FPort 21, then a diagnostic frame on FPort 2 with `cold_boot=true`,
  `boot_counter=1`, `ds18b20_count=1`, `faults=[]`, `ina219_config=0x399F`.

## Follow-ups

- Register/attach the decoder for FPort 1/2 on gisebo-05 in TTN (the v7 formatter
  already contains the branch; TTN routes all FPorts to the one formatter).
- Backend alarm on the diagnostic FPort (extends `docs/backend-monitoring.md`):
  any `faults` non-empty → alert; `reset_cause` = watchdog/brownout trend →
  investigate. Cheaper and louder than inferring faults from the data stream.
- `PERSIST_VERSION` bump means the next flash cold-boots gisebo-05 (expected; a
  flash resets the MCU regardless).
