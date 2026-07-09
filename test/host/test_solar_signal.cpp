// Host tests for solar_signal.h -- EWMA, the bonus gate, and harvest.
//
// The EWMA's whole reason for being time-based is that it must NOT hunt when the
// interval (and thus the sampling rate) changes. Test 3 proves that directly:
// the same day/night cycle sampled at 5 min and at 6 h converges to the same
// value. If it did not, the design would be wrong.
//
// Build and run:  test/host/run_tests.sh

#include "../../solar_signal.h"
#include <cstdio>
#include <cmath>

static int failures = 0;

static void check(bool ok, const char *what) {
  if (ok) { std::printf("  ok    %s\n", what); }
  else    { std::printf("  FAIL  %s\n", what); failures++; }
}

int main() {
  std::printf("\nsolar_signal -- EWMA, bonus gate, harvest\n");

  // -------------------------------------------------------------------------
  // 1. sun_present threshold.
  // -------------------------------------------------------------------------
  check(!sunPresent(0),    "0 mV (night) -> not present");
  check(!sunPresent(3000), "3000 mV -> not present (strictly above)");
  check(sunPresent(3001),  "3001 mV -> present");
  check(sunPresent(4700),  "4700 mV (charger operating point) -> present");
  check(sunPresent(6000),  "6000 mV (panel Voc, charge terminated) -> present");

  // -------------------------------------------------------------------------
  // 2. EWMA basics.
  // -------------------------------------------------------------------------
  {
    check(sunEwmaUpdate(0.5f, true, 0) == 0.5f, "dt=0 leaves the EWMA unchanged");
    // A long stretch of sun pulls toward 1.0; a long dark stretch toward 0.
    float e = 0.0f;
    for (int d = 0; d < 10; d++) e = sunEwmaUpdate(e, true, 86400);  // 10 sunny days
    check(e > 0.99f, "10 sunny days -> EWMA approaches 1.0");
    for (int d = 0; d < 10; d++) e = sunEwmaUpdate(e, false, 86400); // 10 dark days
    check(e < 0.01f, "then 10 dark days -> EWMA approaches 0.0");
  }

  // -------------------------------------------------------------------------
  // 3. THE ONE THAT MATTERS. Same day/night cycle, two very different sampling
  //    rates, must converge to the same steady-state average. This is the whole
  //    reason the window is time-based rather than wake-based.
  // -------------------------------------------------------------------------
  {
    // 16 h day, 8 h night (a summer-ish duty cycle ~0.667).
    //
    // Each phase is credited with its EXACT duration: the final step of a phase
    // is clamped to whatever time remains, so a coarse sampler does not overshoot
    // the day/night boundary and miscount the duty cycle. That clamp mirrors
    // reality -- a wake lands at a real instant with a real dt -- and isolates the
    // property under test (no hunting) from a discretisation artifact.
    auto simulate = [](uint32_t stepS) {
      float e = 0.5f;
      uint32_t dayLen = 16 * 3600, night = 8 * 3600;
      for (int day = 0; day < 60; day++) {
        for (uint32_t t = 0; t < dayLen; t += stepS)
          e = sunEwmaUpdate(e, true,  (t + stepS <= dayLen) ? stepS : (dayLen - t));
        for (uint32_t t = 0; t < night; t += stepS)
          e = sunEwmaUpdate(e, false, (t + stepS <= night)  ? stepS : (night - t));
      }
      return e;
    };
    float fast = simulate(5 * 60);    // sampled every 5 min
    float slow = simulate(6 * 3600);  // sampled every 6 h
    check(std::fabs(fast - slow) < 0.05f,
          "5-min and 6-h sampling converge to the same EWMA (no hunting)");
    check(fast > 0.55f && fast < 0.75f,
          "summer duty cycle settles in a sensible band (~0.6-0.7)");
  }

  // -------------------------------------------------------------------------
  // 4. The bonus gate hysteresis. Engage at 0.55, release at 0.45.
  // -------------------------------------------------------------------------
  {
    check(!sunBonusActive(0.50f, false), "0.50 does NOT engage from off (needs 0.55)");
    check(sunBonusActive(0.55f, false),  "0.55 engages");
    check(sunBonusActive(0.50f, true),   "0.50 STAYS on once active (above release)");
    check(sunBonusActive(0.45f, true),   "0.45 stays on (release is inclusive)");
    check(!sunBonusActive(0.44f, true),  "0.44 releases");
    check(!sunBonusActive(0.50f, false), "and stays off at 0.50 until it climbs back to 0.55");
  }

  // -------------------------------------------------------------------------
  // 5. The gate does not flap on the daily ripple. A summer EWMA rippling
  //    0.55-0.75 must stay ON; a fall EWMA rippling 0.29-0.52 must stay OFF.
  // -------------------------------------------------------------------------
  {
    bool active = false;
    int changes = 0;
    // Summer: engages once, then never releases.
    for (int i = 0; i < 200; i++) {
      float e = 0.65f + 0.10f * std::sin(i * 0.9f);   // 0.55..0.75
      bool next = sunBonusActive(e, active);
      if (next != active) changes++;
      active = next;
    }
    check(changes == 1 && active, "summer ripple 0.55-0.75: engages once, stays on");

    active = false; changes = 0;
    // Fall: peaks at 0.52, never reaches 0.55 -> never engages.
    for (int i = 0; i < 200; i++) {
      float e = 0.40f + 0.12f * std::sin(i * 0.9f);   // 0.28..0.52
      bool next = sunBonusActive(e, active);
      if (next != active) changes++;
      active = next;
    }
    check(changes == 0 && !active, "fall ripple 0.28-0.52: never engages");
  }

  // -------------------------------------------------------------------------
  // 6. Harvest accumulator. Sub-mAh wakes accumulate rather than rounding away.
  // -------------------------------------------------------------------------
  {
    HarvestAccumulator h;
    harvestInit(&h);
    // 30 mA for 30 min = 15 mAh.
    harvestAdd(&h, 30.0f, 1800);
    check(h.totalMah == 15, "30 mA x 30 min = 15 mAh");

    harvestInit(&h);
    // Ten wakes of 6 mA x 5 min = 0.5 mAh each = 5 mAh total. Each is sub-mAh, so
    // this proves they are not lost to rounding.
    for (int i = 0; i < 10; i++) harvestAdd(&h, 6.0f, 300);
    check(h.totalMah == 5, "ten 0.5 mAh wakes accumulate to 5 mAh (no rounding loss)");
  }

  // -------------------------------------------------------------------------
  // 7. Harvest wraps cleanly at 65535 -> 0. The backend unwraps.
  // -------------------------------------------------------------------------
  {
    HarvestAccumulator h;
    harvestInit(&h);
    h.totalMah = 65535;
    harvestAdd(&h, 3600.0f, 3600);   // exactly 3600 mAh -> wraps well past 65535
    check(h.totalMah == (uint16_t)(65535 + 3600), "harvest wraps as a uint16 (backend unwraps)");
  }

  std::printf("\n%s\n\n", failures ? "FAILED" : "all passed");
  return failures ? 1 : 0;
}
