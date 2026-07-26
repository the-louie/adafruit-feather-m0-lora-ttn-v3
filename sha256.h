#pragma once
// Public-domain SHA-256 + HMAC-SHA256 + HKDF (RFC 5869), Arduino-free.
//
// Vendored deliberately so the EXACT SAME code runs on the SAMD21 and in the
// host tests -- a separate Arduino crypto lib would mean the tests validate
// different bytes than the device produces. The SHA-256 core follows Brad
// Conte's widely-used public-domain implementation; HMAC is RFC 2104 and HKDF
// is RFC 5869. Correctness is pinned by known-answer vectors in
// test/host/test_sha256.cpp (FIPS 180-4, RFC 4231, RFC 5869).
//
// hkdf_expand here handles a single output block only (L <= 32), which is all
// the key derivation needs (16-byte AppKey, 8-byte DevEUI).

#include <stdint.h>
#include <stddef.h>
#include <string.h>

namespace crypto {

inline uint32_t rotr32(uint32_t a, uint32_t b) { return (a >> b) | (a << (32 - b)); }

inline constexpr uint32_t SHA256_K[64] = {
  0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
  0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
  0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
  0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
  0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
  0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
  0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
  0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

struct Sha256 {
  uint8_t  data[64];
  uint32_t datalen;
  uint64_t bitlen;
  uint32_t state[8];
};

inline void sha256_transform(Sha256& c, const uint8_t data[64]) {
  uint32_t a, b, cc, d, e, f, g, h, t1, t2, m[64];
  for (uint32_t i = 0, j = 0; i < 16; ++i, j += 4)
    m[i] = ((uint32_t)data[j] << 24) | ((uint32_t)data[j+1] << 16) |
           ((uint32_t)data[j+2] << 8) | ((uint32_t)data[j+3]);
  for (uint32_t i = 16; i < 64; ++i) {
    uint32_t s0 = rotr32(m[i-15],7) ^ rotr32(m[i-15],18) ^ (m[i-15] >> 3);
    uint32_t s1 = rotr32(m[i-2],17) ^ rotr32(m[i-2],19) ^ (m[i-2] >> 10);
    m[i] = m[i-16] + s0 + m[i-7] + s1;
  }
  a=c.state[0]; b=c.state[1]; cc=c.state[2]; d=c.state[3];
  e=c.state[4]; f=c.state[5]; g=c.state[6]; h=c.state[7];
  for (uint32_t i = 0; i < 64; ++i) {
    uint32_t S1 = rotr32(e,6) ^ rotr32(e,11) ^ rotr32(e,25);
    uint32_t ch = (e & f) ^ ((~e) & g);
    t1 = h + S1 + ch + SHA256_K[i] + m[i];
    uint32_t S0 = rotr32(a,2) ^ rotr32(a,13) ^ rotr32(a,22);
    uint32_t maj = (a & b) ^ (a & cc) ^ (b & cc);
    t2 = S0 + maj;
    h=g; g=f; f=e; e=d+t1; d=cc; cc=b; b=a; a=t1+t2;
  }
  c.state[0]+=a; c.state[1]+=b; c.state[2]+=cc; c.state[3]+=d;
  c.state[4]+=e; c.state[5]+=f; c.state[6]+=g; c.state[7]+=h;
}

inline void sha256_init(Sha256& c) {
  c.datalen = 0; c.bitlen = 0;
  c.state[0]=0x6a09e667; c.state[1]=0xbb67ae85; c.state[2]=0x3c6ef372; c.state[3]=0xa54ff53a;
  c.state[4]=0x510e527f; c.state[5]=0x9b05688c; c.state[6]=0x1f83d9ab; c.state[7]=0x5be0cd19;
}

inline void sha256_update(Sha256& c, const uint8_t* data, size_t len) {
  for (size_t i = 0; i < len; ++i) {
    c.data[c.datalen] = data[i];
    c.datalen++;
    if (c.datalen == 64) { sha256_transform(c, c.data); c.bitlen += 512; c.datalen = 0; }
  }
}

inline void sha256_final(Sha256& c, uint8_t hash[32]) {
  uint32_t i = c.datalen;
  c.data[i++] = 0x80;
  if (c.datalen < 56) {
    while (i < 56) c.data[i++] = 0x00;
  } else {
    while (i < 64) c.data[i++] = 0x00;
    sha256_transform(c, c.data);
    memset(c.data, 0, 56);
  }
  c.bitlen += (uint64_t)c.datalen * 8;
  c.data[63] = (uint8_t)(c.bitlen);
  c.data[62] = (uint8_t)(c.bitlen >> 8);
  c.data[61] = (uint8_t)(c.bitlen >> 16);
  c.data[60] = (uint8_t)(c.bitlen >> 24);
  c.data[59] = (uint8_t)(c.bitlen >> 32);
  c.data[58] = (uint8_t)(c.bitlen >> 40);
  c.data[57] = (uint8_t)(c.bitlen >> 48);
  c.data[56] = (uint8_t)(c.bitlen >> 56);
  sha256_transform(c, c.data);
  for (i = 0; i < 8; ++i) {
    hash[i*4+0] = (uint8_t)(c.state[i] >> 24);
    hash[i*4+1] = (uint8_t)(c.state[i] >> 16);
    hash[i*4+2] = (uint8_t)(c.state[i] >> 8);
    hash[i*4+3] = (uint8_t)(c.state[i]);
  }
}

inline void sha256(const uint8_t* msg, size_t len, uint8_t out[32]) {
  Sha256 c; sha256_init(c); sha256_update(c, msg, len); sha256_final(c, out);
}

// HMAC-SHA256 (RFC 2104).
inline void hmac_sha256(const uint8_t* key, size_t keylen,
                        const uint8_t* msg, size_t msglen, uint8_t out[32]) {
  uint8_t k0[64];
  memset(k0, 0, 64);
  if (keylen > 64) sha256(key, keylen, k0);   // long key -> hash, rest stays zero
  else             memcpy(k0, key, keylen);
  uint8_t ipad[64], opad[64];
  for (int i = 0; i < 64; ++i) { ipad[i] = (uint8_t)(k0[i] ^ 0x36); opad[i] = (uint8_t)(k0[i] ^ 0x5c); }
  uint8_t inner[32];
  Sha256 c;
  sha256_init(c); sha256_update(c, ipad, 64); sha256_update(c, msg, msglen); sha256_final(c, inner);
  sha256_init(c); sha256_update(c, opad, 64); sha256_update(c, inner, 32); sha256_final(c, out);
}

// HKDF-Extract (RFC 5869): PRK = HMAC(salt, IKM).
inline void hkdf_extract(const uint8_t* salt, size_t saltLen,
                         const uint8_t* ikm, size_t ikmLen, uint8_t prk[32]) {
  hmac_sha256(salt, saltLen, ikm, ikmLen, prk);
}

// HKDF-Expand (RFC 5869), single block: L <= 32, infoLen small.
inline void hkdf_expand(const uint8_t prk[32], const uint8_t* info, size_t infoLen,
                        uint8_t* okm, size_t L) {
  uint8_t msg[96];
  size_t n = 0;
  memcpy(msg, info, infoLen); n = infoLen;
  msg[n++] = 0x01;               // T(1): counter octet
  uint8_t t[32];
  hmac_sha256(prk, 32, msg, n, t);
  memcpy(okm, t, L);
}

} // namespace crypto
