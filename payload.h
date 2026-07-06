#pragma once
//
// Uplink payload assembly.
//
// Bytes 0-8 belong to the CORE and are byte-identical across both variants.
// A policy appends after byte 8 -- 0 bytes for primary, 6 for solar. Keeping
// that split explicit is what lets v4 add a third contributor (aux sensors)
// without rewriting anything.
//
// Layout (v7):
//   0     interval index, 0-10
//   1-2   12-bit battery offset from 3000 mV, then the 4-bit uplink counter in
//         the low nibble of byte 2
//   3-8   six temperatures, newest first
//   9+    policy-appended (solar only)
//
// Pure logic, no Arduino dependencies, so the host tests exercise the same code
// the firmware runs.
//
#include <stdint.h>

#define PAYLOAD_CORE_LEN 9
#define TEMP_SLOTS 6

// Temperature sentinels. Values 0-200 are real (-10.0 to +30.0 degC at 0.2/step).
#define TEMP_NULL     250
#define TEMP_TOO_COLD 251
#define TEMP_TOO_WARM 252

// Raw sentinels from encodeWaterTemperature(), kept out of the 0-200 range.
#define RAW_INVALID  ((int16_t)0xFFFF)
#define RAW_TOO_COLD ((int16_t)0xFFFE)
#define RAW_TOO_WARM ((int16_t)0xFFFD)

// Water temperature -> centidegrees, or a sentinel.
//
// NAMED FOR WATER ON PURPOSE. The -10..+30 range is a WATER range: a tank or
// lake lives inside it. Air at this site reaches -25 degC and would report
// "too cold" all winter; a sealed box behind a south-facing panel exceeds +30 in
// July. When v4 adds air and box sensors they need their own encoders, and this
// name stops anyone reusing the wrong range by accident.
inline int16_t encodeWaterTemperature(float tempC) {
  if (tempC != tempC || tempC < -50.0f) return RAW_INVALID;   // NaN or disconnected
  if (tempC < -10.0f) return RAW_TOO_COLD;
  if (tempC > 30.0f)  return RAW_TOO_WARM;
  int v = (int)(tempC * 100.0f);
  if (v > 3000)  v = 3000;
  if (v < -1000) v = -1000;
  return (int16_t)v;
}

// Battery millivolts -> 12-bit offset from 3000 mV, clamped both ends.
// 0 = 3.000 V, 4095 = 7.095 V.
//
// Note the top of that range is unreachable in practice: the A7 100k/100k
// divider saturates around 6.59 V. And li-ion only ever uses the bottom third.
// The encoding is kept common across variants regardless -- it costs nothing and
// one decoder path is worth more than a few unused codes.
inline uint16_t encodeBatteryOffset(uint32_t vbat_mv) {
  int32_t offset = (int32_t)vbat_mv - 3000;
  if (offset < 0)    offset = 0;
  if (offset > 4095) offset = 4095;
  return (uint16_t)offset;
}

// One buffered temperature -> its payload byte.
inline uint8_t encodeTempSlot(uint16_t raw) {
  if (raw == (uint16_t)RAW_INVALID)  return TEMP_NULL;
  if (raw == (uint16_t)RAW_TOO_COLD) return TEMP_TOO_COLD;
  if (raw == (uint16_t)RAW_TOO_WARM) return TEMP_TOO_WARM;
  float degC = (int16_t)raw / 100.0f;
  if (degC < -10.0f) return TEMP_TOO_COLD;
  if (degC >  30.0f) return TEMP_TOO_WARM;
  return (uint8_t)((degC + 10.0f) / 0.2f + 0.5f);
}

// Build bytes 0-8. Returns PAYLOAD_CORE_LEN.
//
// `samples` is newest-first; `count` is how many are real. Unfilled slots become
// TEMP_NULL, which the decoder skips -- and because it derives each timestamp
// from the BYTE POSITION, a skipped slot never shifts the others.
inline uint8_t payloadBuildCore(uint8_t *buf,
                                uint8_t intervalIndex,
                                uint32_t vbat_mv,
                                uint8_t uplinkCounter,
                                const uint16_t *samples,
                                uint8_t count) {
  buf[0] = intervalIndex > 10 ? 10 : intervalIndex;

  uint16_t off = encodeBatteryOffset(vbat_mv);
  buf[1] = (uint8_t)(off >> 4);
  buf[2] = (uint8_t)(((off & 0x0FU) << 4) | (uplinkCounter & 0x0FU));

  for (uint8_t i = 0; i < TEMP_SLOTS; i++) {
    buf[3 + i] = (i >= count) ? TEMP_NULL : encodeTempSlot(samples[i]);
  }
  return PAYLOAD_CORE_LEN;
}
