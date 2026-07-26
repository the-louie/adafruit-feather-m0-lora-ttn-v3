# Provision gisebo-05 in TTN and wire its OTAA credentials

**Date:** 2026-07-26 ~16:20 UTC
**Scope:** TTN device registration + firmware credentials + payload formatter. No behaviour change.

## Summary

Created the `gisebo-05` end device in the `telamon-temperature` application (EU868,
`eu1.cloud.thethings.network`), attached the v7 decoder as its uplink formatter, and
set gisebo-05's OTAA credentials as the **active** keys in the sketch. This is the
identity the solar/v7 bench unit joins with; it replaces gisebo-01 at the sprint-08
cutover (the two are never in production together).

## Device identity

| Field | Value |
|---|---|
| Application | `telamon-temperature` |
| Device ID | `gisebo-05` |
| DevEUI | `70B3D57ED0078882` (issued from the application's DevEUI block, so unique) |
| JoinEUI | `0000000000000001` (fleet convention) |
| AppKey | generated 2026-07-26; lives only in the Join Server and in the sketch — **not** recorded here |
| Frequency plan | `EU_863_870_TTN` |
| LoRaWAN / PHY | `MAC_V1_0_3` / `PHY_V1_0_3_REV_A`, OTAA |

Radio settings mirror gisebo-01 exactly (read back from its NS registration first).

## What changed in the firmware

`adafruit-feather-m0-lora-ttn-2.ino` OTAA block:

- `DEVEUI[]` (little-endian) and `APPKEY[]` (big-endian) now hold gisebo-05's keys and are the uncommented, active set.
- gisebo-01's keys are retained but **commented out and labelled "PRODUCTION, frozen — DO NOT flash from v7"**, so this binary cannot accidentally impersonate the live production unit. Board 4 stays commented.
- `APPEUI[]` (JoinEUI) unchanged (`{0x01,0x00,…}` = `0000000000000001`).

## Rationale

gisebo-05 had no TTN identity yet, so an OTAA join was impossible — the blocker
before any bench bring-up. Registering it and pinning its keys into the sketch (the
existing hardcoded-credentials pattern; see "Known gaps" in `CLAUDE.md`) clears that.
DevEUI was issued from the app block rather than hand-rolled to guarantee uniqueness
within TTN's `70B3D57ED0…` range.

## Verification

- Registration returned **HTTP 200** on all four servers (IS create, JS root keys, NS settings, AS register); device confirmed present via a read-back.
- Uplink formatter set to `FORMATTER_JAVASCRIPT` (the `decoders/gisebo-05-v7.js` source, 6153 bytes) and read back.
- Sketch compiles: **69560 bytes (26%)** of program storage (`arduino-cli compile --fqbn adafruit:samd:adafruit_feather_m0 .`).
- gisebo-01/02/03/04 were read only, never modified.

## Follow-ups

- Bench bring-up: strap DEV (pin 11 → GND), USB power, battery unplugged; flash and watch for the join + first uplink on FPort 21, decoded by the attached formatter.
- Revoke `TTN_FULL_ACCESS_TOKEN` in `.env` once provisioning/bring-up is complete (expires 2026-08-31 regardless).
- Longer term, `docs/generate-keys-from-feather-serial.md` proposes deriving keys from the SAMD21 serial so one binary serves every board and no per-board key edit is needed.
