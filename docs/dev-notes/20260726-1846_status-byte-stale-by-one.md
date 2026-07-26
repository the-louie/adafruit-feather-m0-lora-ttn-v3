# Solar status byte was built one cycle before its inputs were set

**Date:** 2026-07-26 ~18:46 UTC
**Scope:** `transmitBatchAndWait()` ordering fix. No protocol change.

## Summary

In `transmitBatchAndWait()` the solar status byte (payload byte 14) was assembled
by `policy->appendPayload()` **before** the `.ino` populated
`solarPolicy.bootCounter_` / `statusFlags_`. Those members keep their previous
value between uplinks, so every status byte reported the *previous* cycle's flags
— and **zero on the very first uplink after boot** (the C++ default member
initialisers). The inline comment already said the inputs must be set "before the
payload is built"; the code did it after. Moved the population block above the
`appendPayload()` call.

## Evidence (live, not reasoning)

gisebo-05's first post-join uplink, pulled from TTN Storage (f_cnt 0, FPort 21,
2026-07-26T18:12:47Z):

```
payload: 02 48 40 9E FA FA FA FA FA A9 00 01 00 00 00
byte 14 (status) = 0x00  ->  boot_counter 0, cold_boot false, clock_valid false
```

`setup()` increments `persist.bootCounter` to 1 on a cold boot and the status
logic sets `STATUS_COLD_BOOT` while `bootCounter <= 1`, so byte 14 should have
been `0x21` (boot_counter 1 + cold_boot). It read `0x00` because `appendPayload()`
consumed the default-initialised members.

## Effect

- First uplink after every boot reported `boot_counter 0` + all flags clear,
  regardless of the real boot count — so `cold_boot` never fired on the frame
  that actually *is* the cold boot.
- `clock_valid` (which can flip mid-session when a `DeviceTimeReq` lands) was
  reported one uplink late.
- Primary variant is unaffected — it has no status byte at all (a separate gap,
  addressed by the diagnostics FPort work).

## Fix

Moved the `if (powerVariant == VARIANT_SOLAR) { ... statusFlags_ = flags; }` block
to immediately **before** `len += policy->appendPayload(...)`. The status byte now
reflects the current cycle.

## Verification

- Host tests unchanged and still green (the ordering lives in the `.ino` glue, not
  in `policy_solar.h`; `appendPayload` itself was always correct given its inputs).
- Compile-clean (`arduino-cli compile --fqbn adafruit:samd:adafruit_feather_m0 .`).
- Field check pending: the next post-flash cold boot must show `boot_counter 1` +
  `cold_boot true` on the **first** uplink, not the second.

## Predicted confirmation on current (un-fixed) firmware

The next data uplink from the currently-flashed build should show `boot_counter 1`
+ `cold_boot true` (this boot's values, arriving one cycle late) — the signature
of the stale-by-one bug. That prediction is the pre-fix baseline.
