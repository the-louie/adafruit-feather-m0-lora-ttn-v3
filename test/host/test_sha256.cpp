// Host tests for sha256.h -- known-answer vectors from the standards.
//
// If any of these fail, the crypto is wrong and every derived key is wrong.
// Vectors: FIPS 180-4 (SHA-256), RFC 4231 (HMAC-SHA256), RFC 5869 (HKDF).
//
// Build and run:  test/host/run_tests.sh

#include "../../sha256.h"
#include <cstdio>
#include <cstring>

static int failures = 0;

static void check(bool ok, const char *what) {
  if (ok) { std::printf("  ok    %s\n", what); }
  else    { std::printf("  FAIL  %s\n", what); failures++; }
}

// Parse an even-length hex string into bytes.
static size_t unhex(const char *hex, uint8_t *out) {
  size_t n = 0;
  for (const char *p = hex; p[0] && p[1]; p += 2) {
    auto nib = [](char c) -> int {
      if (c >= '0' && c <= '9') return c - '0';
      if (c >= 'a' && c <= 'f') return c - 'a' + 10;
      if (c >= 'A' && c <= 'F') return c - 'A' + 10;
      return 0;
    };
    out[n++] = (uint8_t)((nib(p[0]) << 4) | nib(p[1]));
  }
  return n;
}

static bool eqHex(const uint8_t *got, const char *expectHex, size_t n, const char *label) {
  uint8_t exp[64];
  unhex(expectHex, exp);
  for (size_t i = 0; i < n; i++) {
    if (got[i] != exp[i]) {
      std::printf("          %s: byte[%zu] expected %02x, got %02x\n", label, i, exp[i], got[i]);
      return false;
    }
  }
  return true;
}

int main() {
  std::printf("\nsha256 -- SHA-256 / HMAC / HKDF known-answer vectors\n");

  // --- SHA-256, FIPS 180-4 examples ---
  {
    uint8_t h[32];
    crypto::sha256((const uint8_t *)"abc", 3, h);
    check(eqHex(h, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", 32, "sha256(abc)"),
          "SHA-256(\"abc\")");

    crypto::sha256((const uint8_t *)"", 0, h);
    check(eqHex(h, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", 32, "sha256(empty)"),
          "SHA-256(\"\")");

    // A message that crosses the 56-byte padding boundary (two blocks).
    const char *m = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    crypto::sha256((const uint8_t *)m, std::strlen(m), h);
    check(eqHex(h, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1", 32, "sha256(2-block)"),
          "SHA-256(56-byte message, two blocks)");
  }

  // --- HMAC-SHA256, RFC 4231 test case 2 ---
  {
    uint8_t mac[32];
    crypto::hmac_sha256((const uint8_t *)"Jefe", 4,
                        (const uint8_t *)"what do ya want for nothing?", 28, mac);
    check(eqHex(mac, "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843", 32, "hmac"),
          "HMAC-SHA256 (RFC 4231 case 2)");
  }

  // --- HKDF-SHA256, RFC 5869 test case 1 ---
  {
    uint8_t ikm[22], salt[13], info[10];
    for (int i = 0; i < 22; i++) ikm[i]  = 0x0b;
    for (int i = 0; i < 13; i++) salt[i] = (uint8_t)i;         // 00 01 .. 0c
    for (int i = 0; i < 10; i++) info[i] = (uint8_t)(0xf0 + i); // f0 .. f9

    uint8_t prk[32];
    crypto::hkdf_extract(salt, 13, ikm, 22, prk);
    check(eqHex(prk, "077709362c2e32df0ddc3f0dc47bba6390b6c73bb50f9c3122ec844ad7c2b3e5", 32, "prk"),
          "HKDF-Extract PRK (RFC 5869 case 1)");

    // First output block (32 bytes) of the case-1 OKM.
    uint8_t okm[32];
    crypto::hkdf_expand(prk, info, 10, okm, 32);
    check(eqHex(okm, "3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db02d56ecc4c5bf", 32, "okm"),
          "HKDF-Expand first block (RFC 5869 case 1)");
  }

  std::printf(failures ? "\nSHA256: %d FAILURE(S)\n\n" : "\nsha256: all passed\n\n", failures);
  return failures ? 1 : 0;
}
