# INA219 misdetected as absent after a warm reset — probe now soft-resets first

**Date:** 2026-07-27 ~18:48 UTC
**Scope:** `variant_probe.h` invariant + `.ino` `probeIna219Once()` fix. Confirmed
on hardware (gisebo-05), caught by the new verbose diagnostics frame (item 16).

## Symptom

After flashing gisebo-05 (solar), it uplinked on **FPort 20 (PRIMARY)** instead of
FPort 21 (solar) following RST-button resets — the A1 misdetect: a solar unit
booting the primary policy, parking at a wrong interval (index 6 / 120 min because
a 3.75 V li-ion reads "low" against the 5.0/4.3/3.5 V primary bands).

## Root cause (from the verbose frame, not reasoning)

The FPort-3 verbose frame reported `ina219_config = 0x019F`. That is **exactly**
what `setCalibration_16V_400mA()` writes:

```
BVOLTAGERANGE_16V(0x0000) | GAIN_1_40MV(0x0000) | BADCRES_12BIT(0x0180)
| SADCRES_12BIT_1S_532US(0x0018) | MODE_SANDBVOLT_CONTINUOUS(0x0007) = 0x019F
```

So the INA219 was present, healthy, and cleanly readable — it was holding the
config the firmware itself wrote. The probe, however, accepts **only** the
power-on reset default `0x399F`:

- **Cold boot** (fresh power): config = `0x399F` → probe passes → SOLAR.
- **Warm reset** (RST button, watchdog, **the PROD join-failure
  `NVIC_SystemReset()`**): the INA219 stays powered and still holds `0x019F` from
  the previous boot's `setCalibration` → probe reads `0x019F ≠ 0x399F` → fails →
  PRIMARY.

This is a real field defect: a solar unit that **fails its first join** does
`NVIC_SystemReset()` and would misdetect as primary on the reboot — silently,
until the A1 alarm or a cold power-cycle.

## Fix

`probeIna219Once()` now **soft-resets the INA219 before reading** — writes RST
(config bit 15, `0x8000`) and waits 1 ms, so a present sensor returns to `0x399F`
regardless of prior state. The decision logic is unchanged (still requires
`0x399F`). `variant_probe.h` documents the invariant + `INA219_CONFIG_CALIBRATED_VALUE
0x019F`; `test_variant_probe.cpp` gains a regression: the calibrated `0x019F` is
rejected (so the `.ino` must soft-reset), and `0x399F` after reset is recognised.

## Verification

- Host tests green (incl. the new regression).
- Compiles.
- On hardware: after flashing, a warm RST-button reset must now come up SOLAR
  (FPort 21, `ina219_seen: true`) rather than PRIMARY. (This is what the fixed
  `.bin` is being flashed to confirm.)

## Immediate workaround (pre-fix firmware)

A **full power-cycle** (not RST) resets the INA219 to `0x399F`, so the current
build boots SOLAR after unplug/replug.
