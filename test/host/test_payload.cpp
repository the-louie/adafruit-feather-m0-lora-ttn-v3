// Host tests for payload.h -- byte-level assembly.
//
// The assertion that matters: the refactored builder must reproduce REAL
// PRODUCTION BYTES exactly. If it does not, the refactor changed the wire
// format, and every deployed decoder is now wrong.
//
// Build and run:  test/host/run_tests.sh

#include "../../payload.h"
#include <cstdio>
#include <cmath>

static int failures = 0;

static void check(bool ok, const char *what) {
  if (ok) { std::printf("  ok    %s\n", what); }
  else    { std::printf("  FAIL  %s\n", what); failures++; }
}

static bool bytesEqual(const uint8_t *a, const uint8_t *b, int n, const char *label) {
  for (int i = 0; i < n; i++) {
    if (a[i] != b[i]) {
      std::printf("          %s: byte[%d] expected %d, got %d\n", label, i, b[i], a[i]);
      return false;
    }
  }
  return true;
}

int main() {
  std::printf("\npayload -- byte-level assembly\n");

  // -------------------------------------------------------------------------
  // 1. REAL PRODUCTION BYTES. gisebo-01, f_cnt=376, captured 2026-07-15:
  //      [4, 173, 7, 134, 134, 134, 134, 135, 135]
  //    interval index 4, battery 5.768 V, counter 7, six temps ~16.8-17.0 C
  //
  //    Reconstruct it from the inputs and assert byte-for-byte.
  // -------------------------------------------------------------------------
  {
    // 134 -> (134*0.2)-10 = 16.8 C -> stored as centidegrees 1680
    // 135 -> 17.0 C -> 1700
    uint16_t samples[6] = {1680, 1680, 1680, 1680, 1700, 1700};
    uint8_t buf[16] = {0};
    uint8_t n = payloadBuildCore(buf, 4, 5768, 7, samples, 6);

    const uint8_t expected[9] = {4, 173, 7, 134, 134, 134, 134, 135, 135};
    check(n == 9, "core payload is 9 bytes");
    check(bytesEqual(buf, expected, 9, "gisebo-01 f_cnt=376"),
          "REAL production bytes reproduced exactly (gisebo-01 f_cnt=376)");
  }

  // -------------------------------------------------------------------------
  // 2. Real short batch. gisebo-01, f_cnt=378:
  //      [4, 172, 161, 136, 135, 135, 135, 250, 250]
  //    Only 4 samples; slots 4 and 5 are TEMP_NULL. Note byte[2]=161 packs the
  //    battery low nibble (10) with counter 1.
  // -------------------------------------------------------------------------
  {
    uint16_t samples[6] = {1720, 1700, 1700, 1700, 0, 0};
    uint8_t buf[16] = {0};
    payloadBuildCore(buf, 4, 5762, 1, samples, 4);

    const uint8_t expected[9] = {4, 172, 161, 136, 135, 135, 135, 250, 250};
    check(bytesEqual(buf, expected, 9, "gisebo-01 f_cnt=378"),
          "REAL short batch reproduced exactly, nulls in slots 4-5");
  }

  // -------------------------------------------------------------------------
  // 3. Battery encoding.
  // -------------------------------------------------------------------------
  check(encodeBatteryOffset(3000) == 0,    "3.000 V -> offset 0");
  check(encodeBatteryOffset(5768) == 2768, "5.768 V -> offset 2768 (gisebo-01)");
  check(encodeBatteryOffset(2500) == 0,    "below 3.000 V clamps to 0, not negative");
  check(encodeBatteryOffset(9000) == 4095, "above 7.095 V clamps to 4095");
  check(encodeBatteryOffset(4000) == 1000, "li-ion 4.000 V -> offset 1000 (bottom third)");

  // -------------------------------------------------------------------------
  // 4. Temperature encoding and its sentinels.
  // -------------------------------------------------------------------------
  // 1679, NOT 1680. `(int)(16.8f * 100.0f)` truncates: 16.8 as float32 is
  // 16.79999924, so x100 gives 1679.99992 and the cast drops the fraction.
  // The ORIGINAL does exactly this, so it is preserved rather than "fixed" --
  // S02-13's rule is behaviour-identical while moving.
  //
  // It is also harmless: encodeTempSlot's +0.5 rounding absorbs it, so the
  // payload byte is still 134. That is why the round-trip test below passes.
  // My first draft asserted 1680 and failed; the code was right.
  check(encodeWaterTemperature(16.8f) == 1679, "16.8 C -> 1679 (float truncation, as the original)");
  check(encodeTempSlot((uint16_t)encodeWaterTemperature(16.8f)) == 134,
        "...and the payload byte is still 134 -- truncation is absorbed by slot rounding");
  check(encodeWaterTemperature(-10.0f) == -1000, "-10.0 C is the low rail, not a sentinel");
  check(encodeWaterTemperature(30.0f) == 3000, "30.0 C is the high rail, not a sentinel");
  check(encodeWaterTemperature(30.1f) == RAW_TOO_WARM, "30.1 C -> too warm");
  check(encodeWaterTemperature(-10.1f) == RAW_TOO_COLD, "-10.1 C -> too cold");
  check(encodeWaterTemperature(NAN) == RAW_INVALID, "NaN -> invalid");
  check(encodeWaterTemperature(-127.0f) == RAW_INVALID, "-127 (DS18B20 disconnect) -> invalid");

  // 85.0 is the DS18B20's power-on default -- the value the idle(750) defect
  // produced on the first read after boot. It must encode as "too warm", which
  // is the 252 signature S01-08's alarm looks for.
  check(encodeWaterTemperature(85.0f) == RAW_TOO_WARM,
        "85.0 (DS18B20 power-on default) -> too warm == the 252 signature");

  // -------------------------------------------------------------------------
  // 5. Slot encoding, including the rails.
  // -------------------------------------------------------------------------
  check(encodeTempSlot((uint16_t)-1000) == 0,   "-10.0 C -> slot 0 (low rail)");
  check(encodeTempSlot(3000) == 200,            "+30.0 C -> slot 200 (high rail)");
  check(encodeTempSlot(0) == 50,                "0.0 C -> slot 50");
  check(encodeTempSlot(1680) == 134,            "16.8 C -> slot 134 (real gisebo-01)");
  check(encodeTempSlot((uint16_t)RAW_INVALID) == TEMP_NULL,   "invalid -> 250");
  check(encodeTempSlot((uint16_t)RAW_TOO_COLD) == TEMP_TOO_COLD, "too cold -> 251");
  check(encodeTempSlot((uint16_t)RAW_TOO_WARM) == TEMP_TOO_WARM, "too warm -> 252");

  // -------------------------------------------------------------------------
  // 6. Round trip through the decoder's own arithmetic, across the full range.
  //    (v * 0.2) - 10 is exactly what the live decoders compute.
  // -------------------------------------------------------------------------
  {
    bool allWithinQuantisation = true;
    for (float t = -10.0f; t <= 30.0f; t += 0.1f) {
      uint8_t slot = encodeTempSlot((uint16_t)encodeWaterTemperature(t));
      float back = (slot * 0.2f) - 10.0f;
      if (std::fabs(back - t) > 0.1f + 1e-4f) allWithinQuantisation = false;
    }
    check(allWithinQuantisation,
          "round trip -10..+30 C stays within the 0.2 C quantisation step");
  }

  // -------------------------------------------------------------------------
  // 7. The interval byte clamps rather than wrapping.
  // -------------------------------------------------------------------------
  {
    uint16_t s[6] = {0, 0, 0, 0, 0, 0};
    uint8_t buf[16] = {0};
    payloadBuildCore(buf, 200, 5000, 0, s, 6);
    check(buf[0] == 10, "interval index > 10 clamps to 10, does not wrap");
  }

  // -------------------------------------------------------------------------
  // 8. The counter occupies only the low nibble and cannot corrupt the battery.
  // -------------------------------------------------------------------------
  {
    uint16_t s[6] = {0, 0, 0, 0, 0, 0};
    uint8_t a[16] = {0}, b[16] = {0};
    payloadBuildCore(a, 4, 5768, 0,  s, 6);
    payloadBuildCore(b, 4, 5768, 15, s, 6);
    check(a[1] == b[1], "counter does not touch byte 1 (battery high bits)");
    check((a[2] >> 4) == (b[2] >> 4), "counter does not touch the battery low nibble");
    check((b[2] & 0x0F) == 15, "counter 15 lands in the low nibble");
  }

  std::printf("\n%s\n\n", failures ? "FAILED" : "all passed");
  return failures ? 1 : 0;
}
