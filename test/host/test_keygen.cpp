// Host tests for keygen.h -- credential derivation.
//
// Expected values are computed INDEPENDENTLY in Python (hmac/hashlib) so this
// suite cross-checks the C++ HKDF against a second implementation, not against
// itself. Uses a throwaway TEST salt; the production salt never appears here.
//
// Build and run:  test/host/run_tests.sh

#include "../../keygen.h"
#include <cstdio>
#include <cstring>

static int failures = 0;

static void check(bool ok, const char *what) {
  if (ok) { std::printf("  ok    %s\n", what); }
  else    { std::printf("  FAIL  %s\n", what); failures++; }
}

static bool eqHex(const uint8_t *got, const char *expectHex, size_t n, const char *label) {
  auto nib = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
  };
  for (size_t i = 0; i < n; i++) {
    uint8_t e = (uint8_t)((nib(expectHex[i*2]) << 4) | nib(expectHex[i*2+1]));
    if (got[i] != e) {
      std::printf("          %s: byte[%zu] expected %02x, got %02x\n", label, i, e, got[i]);
      return false;
    }
  }
  return true;
}

int main() {
  std::printf("\nkeygen -- derive DevEUI + AppKey from silicon serial\n");

  // TEST salt only -- not the production salt.
  const uint8_t *SALT = reinterpret_cast<const uint8_t *>("test-salt-DO-NOT-SHIP");
  const size_t   SALT_LEN = 21;

  uint8_t serial[16];
  for (int i = 0; i < 16; i++) serial[i] = (uint8_t)i;   // 0001..0f

  DerivedCreds c;
  deriveCredentials(serial, SALT, SALT_LEN, c);

  // --- Cross-check against the Python-computed vectors ---
  check(eqHex(c.appKey, "d6ede02aecad120472d1674ce4a239d3", 16, "appkey"),
        "AppKey matches independent (Python) HKDF vector");
  check(eqHex(c.devEui, "b29d16a78e2581e9", 8, "deveui"),
        "DevEUI matches independent (Python) HKDF vector");

  // --- DevEUI is a well-formed locally-administered, individual EUI-64 ---
  check((c.devEui[0] & 0x02) != 0, "DevEUI is locally-administered (U/L bit set)");
  check((c.devEui[0] & 0x01) == 0, "DevEUI is individual, not group (I/G bit clear)");

  // --- Determinism: same inputs -> same output ---
  DerivedCreds c2;
  deriveCredentials(serial, SALT, SALT_LEN, c2);
  check(std::memcmp(&c, &c2, sizeof(DerivedCreds)) == 0, "derivation is deterministic");

  // --- A different serial yields different keys (no collision) ---
  uint8_t serial2[16];
  std::memcpy(serial2, serial, 16); serial2[0] = 0xff;
  DerivedCreds d;
  deriveCredentials(serial2, SALT, SALT_LEN, d);
  check(eqHex(d.appKey, "cc5693bc241d8d8d4dd59cf0583a2fcc", 16, "appkey2"),
        "different serial -> Python-matched different AppKey");
  check(std::memcmp(c.appKey, d.appKey, 16) != 0, "different serial -> different AppKey");
  check(std::memcmp(c.devEui, d.devEui, 8)  != 0, "different serial -> different DevEUI");

  // --- Salt sensitivity: change the salt -> keys change ---
  DerivedCreds e;
  deriveCredentials(serial, reinterpret_cast<const uint8_t *>("other-salt"), 10, e);
  check(std::memcmp(c.appKey, e.appKey, 16) != 0, "different salt -> different AppKey");

  // --- LMIC byte-order helper reverses MSB-first to LSB-first ---
  uint8_t le[8];
  euiToLmicLE(c.devEui, le);
  bool reversed = true;
  for (int i = 0; i < 8; i++) if (le[i] != c.devEui[7 - i]) reversed = false;
  check(reversed, "euiToLmicLE reverses byte order for LMIC");

  std::printf(failures ? "\nKEYGEN: %d FAILURE(S)\n\n" : "\nkeygen: all passed\n\n", failures);
  return failures ? 1 : 0;
}
