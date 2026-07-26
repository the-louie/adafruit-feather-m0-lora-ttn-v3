#pragma once
// Derive a board's OTAA credentials from its SAMD21 silicon serial.
//
// One universal binary: every board reads its own permanent 128-bit serial and
// reconstructs its DevEUI + AppKey on boot, so no per-board key edits and no
// risk of flashing the wrong keys. Security rests entirely on the shared secret
// SALT staying private (the serial is public -- it is printed and SWD-readable),
// so keep the salt and the binary private. See docs/generate-keys-from-feather-serial.md.
//
// Derivation is HKDF-SHA256 with per-field domain separation:
//     PRK    = HKDF-Extract(salt, serial16)
//     AppKey = HKDF-Expand(PRK, "gisebo-appkey", 16)
//     DevEUI = HKDF-Expand(PRK, "gisebo-deveui", 8), shaped locally-administered
//
// This is Arduino-free and host-tested (test/host/test_keygen.cpp). The JoinEUI
// is NOT derived -- it is a fixed fleet-wide constant kept in the sketch.

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "sha256.h"

struct DerivedCreds {
  uint8_t devEui[8];   // canonical MSB-first (the byte order the TTN console shows)
  uint8_t appKey[16];  // canonical MSB-first
};

inline void deriveCredentials(const uint8_t serial[16],
                              const uint8_t* salt, size_t saltLen,
                              DerivedCreds& out) {
  uint8_t prk[32];
  crypto::hkdf_extract(salt, saltLen, serial, 16, prk);

  crypto::hkdf_expand(prk, reinterpret_cast<const uint8_t*>("gisebo-appkey"), 13, out.appKey, 16);

  uint8_t de[8];
  crypto::hkdf_expand(prk, reinterpret_cast<const uint8_t*>("gisebo-deveui"), 13, de, 8);
  // Shape into a well-formed EUI-64 that claims no OUI: clear the I/G bit
  // (individual, not group) and set the U/L bit (locally administered).
  de[0] = (uint8_t)((de[0] & ~0x01) | 0x02);
  memcpy(out.devEui, de, 8);
}

// LMIC's os_getDevEui() and os_getArtEui() want their EUI-64 little-endian
// (LSB first); the canonical form above is MSB first. AppKey needs no reversal.
inline void euiToLmicLE(const uint8_t euiMsb[8], uint8_t lmicLE[8]) {
  for (int i = 0; i < 8; ++i) lmicLE[i] = euiMsb[7 - i];
}
