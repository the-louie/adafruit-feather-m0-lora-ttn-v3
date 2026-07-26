# Derive OTAA keys from the SAMD21 silicon serial

**Date:** 2026-07-26
**Scope:** replace hardcoded per-board OTAA keys with on-boot derivation. One universal binary.

## Summary

The sketch no longer carries per-board DevEUI/AppKey arrays. On boot it reads the
SAMD21's 128-bit factory serial and derives its DevEUI + AppKey via HKDF-SHA256
with a shared secret salt, into `g_creds`, before LMIC joins. In DEV it prints the
derived keys for TTN registration. Implements `docs/generate-keys-from-feather-serial.md`.

## Design decisions (approved 2026-07-26)

- **Fork 1A — DevEUI namespace:** locally-administered, individual EUI-64 derived
  from the serial (`devEui[0] = (b & ~0x01) | 0x02`). Claims no OUI, so it cannot
  collide with TTN-issued `70B3D57ED0…` DevEUIs.
- **Fork 2A — gisebo-05:** re-provisioned in TTN to its *derived* keys rather than
  keeping the block DevEUI/random AppKey we had registered, so the fleet is a true
  one-binary system with nothing hardcoded.
- **KDF:** HKDF-SHA256, per-field domain separation (`"gisebo-appkey"` / `"gisebo-deveui"`).
- **Crypto:** vendored `sha256.h` (SHA-256 + HMAC + HKDF), so on-device and host
  code are identical.

## Files

- `sha256.h`, `keygen.h` — Arduino-free, host-tested.
- `keygen_salt.h` — **gitignored** secret salt (template: `keygen_salt.h.example`).
- `adafruit-feather-m0-lora-ttn-2.ino` — `readChipSerial()`, `deriveBoardCredentials()`,
  rewritten `os_getDevEui/os_getDevKey`, DEV printout. JoinEUI unchanged.
- `test/host/test_sha256.cpp`, `test/host/test_keygen.cpp`.

## Verification

- **Crypto KATs pass:** SHA-256 (FIPS 180-4), HMAC (RFC 4231), HKDF (RFC 5869).
- **Derivation cross-checked** against an independent Python HKDF implementation;
  plus determinism, salt-sensitivity, collision, DevEUI locally-administered/individual
  bits, and byte-order tests. Full host suite green.
- **Sketch compiles:** 71156 bytes (27%), up ~1.6 KB from the crypto.

## Security note

The chip serial is public (printed, SWD-readable), so all security rests on the
salt. `keygen_salt.h` is gitignored and must be backed up offline; the repo/binary
must stay private. Host tests use a throwaway test salt.

## Follow-up / caveat

- `readChipSerial()`'s byte order is assumed identical to the Arduino SAMD core's
  USB-serial construction (same four words, MSW first). The DEV printout at first
  flash is the source of truth — **verify the printed DevEUI/AppKey match the
  pre-computed re-provisioned values**, and re-provision if the order differs.
- gisebo-01 (frozen production) is never reflashed with this firmware, so it is
  unaffected; its keys stay as registered.
