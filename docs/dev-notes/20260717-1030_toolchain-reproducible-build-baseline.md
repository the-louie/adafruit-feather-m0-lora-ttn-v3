# Toolchain reproducible, build baseline recorded (S01-00)

## Summary

The project compiles. `scripts/setup-toolchain.sh` takes a bare machine to a green build with no root, and the flash baseline is recorded for the first time.

```
Sketch uses 61632 bytes (23%) of program storage space. Maximum is 262144 bytes.
```

RAM is not reported by `arduino-cli` for SAMD (no static estimate for this core) — so the `.noinit` work in sprint 03 has no RAM headroom figure to compare against. Not blocking, but worth knowing before adding a persistent struct.

## Versions — pinned as of 2026-07-17

| component | version |
|---|---|
| adafruit:samd core | 1.7.17 |
| MCCI LoRaWAN LMIC | **6.0.1** |
| Arduino Low Power | 1.2.2 |
| RTCZero | 1.6.0 |
| OneWire | 2.3.8 |
| DallasTemperature | 4.0.6 |
| arduino-cli | 1.5.1 |

**MCCI LMIC is 6.0.1**, which matches the version the DeviceTimeReq contract in TODO #6 was verified against. `LMIC_ENABLE_DeviceTimeReq` defaults to 1 and the callback carries no time — both confirmed against this exact tree, not a guessed one.

## The trap: a fresh install is a US915 build

`lmic_project_config.h` does **not** ship with every region commented out. It ships with **`CFG_us915 1` enabled**:

```
//#define CFG_eu868 1
#define CFG_us915 1        <-- stock default
```

So "install the libraries and enable eu868" produces **two** defined regions and LMIC's own guard fires: `#error You can define at most one of CFG_... variables`. The first run of the setup script hit exactly this. The script now disables every region before enabling one, and asserts exactly one is left. `CFG_sx1276_radio` is not a region — it selects the RFM95's radio chip and must stay.

This matters beyond convenience. The build config lives inside the library, not the repo: it is invisible, unversioned, and not reproducible from a clone. `reference/lmic_project_config.h` is now committed as the known-good copy.

## Verification — the guard was proven, not assumed

The sketch's `#ifndef CFG_eu868 / #error` guard is load-bearing, so it was tested rather than trusted:

- **No region defined** → the sketch's `#error` fires. Build refused.
- **Stock default (us915 only)** → `FATAL: This firmware explicitly requires CFG_eu868`. Build refused.
- **eu868 only** → compiles, 61632 bytes.

The middle case is the important one: the exact configuration a fresh install produces cannot silently yield a US915 binary. It fails loudly.

## Notes

`downloads.arduino.cc` was unreachable from the sandbox on the first attempt; egress was opened and the run completed. The script is idempotent and safe to re-run.

Root is not needed for any of this. `scripts/setup-flashing-root.sh` covers the only part that does — dialout and udev — and is not needed until sprint 06.
