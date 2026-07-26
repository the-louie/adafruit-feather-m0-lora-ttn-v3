# Deriving OTAA keys from the SAMD21 silicon serial

**Status: implemented** (2026-07-26). Replaces the previous proposal.

Hardcoding per-board keys means eventually flashing the wrong keys to the wrong
board and silently breaking its join. Instead, every board derives its own
DevEUI + AppKey on boot from its permanent 128-bit silicon serial mixed with a
shared secret salt. **One binary, flashed to every board**, and each reconstructs
its correct keys.

## How it works

- **`readChipSerial()`** (in the sketch) reads the SAMD21's factory serial — four
  words at `0x0080A00C`, `0x0080A040`, `0x0080A044`, `0x0080A048` — into 16 bytes,
  most-significant word first. (This is the same value the Arduino SAMD core
  publishes as the USB serial number, so a board's derived keys can be computed
  from its USB `iSerial` before it is even flashed.)
- **`keygen.h`** (`deriveCredentials()`, host-tested, Arduino-free) runs **HKDF-SHA256**
  with per-field domain separation:
  ```
  PRK    = HKDF-Extract(salt, serial16)
  AppKey = HKDF-Expand(PRK, "gisebo-appkey", 16)
  DevEUI = HKDF-Expand(PRK, "gisebo-deveui", 8)   then shaped (see below)
  ```
- **DevEUI shape (fork 1A):** the 8 derived bytes are made a well-formed
  **locally-administered, individual EUI-64** — clear the I/G bit, set the U/L bit
  (`devEui[0] = (b & ~0x01) | 0x02`). This claims no IEEE OUI block, so it can
  never collide with a TTN-issued `70B3D57ED0…` DevEUI.
- **JoinEUI is not derived** — it is the fixed fleet constant `0000000000000001`,
  kept in the sketch.
- **Endianness:** `deriveCredentials()` emits canonical **MSB-first** DevEUI/AppKey
  (what the TTN console shows and what DEV prints). `os_getDevEui()` byte-reverses
  to LMIC's little-endian via `euiToLmicLE()`; the AppKey needs no reversal.

The crypto is vendored in `sha256.h` (SHA-256 + HMAC + HKDF), so the exact same
code runs on-device and in the host tests. Correctness is pinned by known-answer
vectors: FIPS 180-4, RFC 4231, RFC 5869 (`test/host/test_sha256.cpp`), and the
derivation is cross-checked against an independent Python implementation
(`test/host/test_keygen.cpp`).

## The salt is the crown jewel

The chip serial is **public** — it is printed at boot and readable over SWD.
So the security of every board's AppKey rests **entirely on the salt** staying
secret. Anyone with the compiled binary or the salt can reconstruct any board's
AppKey from its serial. This is acceptable for a private fleet **only if the
binary and repo stay private**.

- The real salt lives in **`keygen_salt.h`**, which is **gitignored** — an
  unversioned required file, exactly like `lmic_project_config.h`. The build
  will not compile without it.
- Copy `keygen_salt.h.example` → `keygen_salt.h` and fill in 32 random bytes
  (`openssl rand -hex 32`). **Back the salt up offline** — losing it means every
  board must be re-registered; leaking it compromises every AppKey.
- Host tests use a separate throwaway test salt, so the real salt never appears
  in the repo or the test vectors.

## Workflow: registering a new board

1. Ensure `keygen_salt.h` exists, then flash the universal firmware (any board).
2. Strap **DEV** (pin 11 → GND) and open the serial monitor at 9600. The board
   prints:
   ```
   DevEUI (MSB, register in TTN): 86A2A75D253A16AC
   AppKey (MSB): 3A76C5A52F230150DCAF568A1FF17082
   JoinEUI: 0000000000000001
   ```
3. Register the device in TTN (`telamon-temperature`, EU868): **Enter end device
   specifics manually**, Frequency plan **Europe 863-870 MHz (SF9 for RX2)**,
   LoRaWAN **MAC V1.0.3**, JoinEUI `0000000000000001`, then paste the printed
   DevEUI and AppKey. Attach the device's decoder from `decoders/`.

Because the DevEUI/AppKey are a pure function of `(serial, salt)`, a re-flash two
years later reconstructs the same keys — no re-registration needed.
