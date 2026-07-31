// Host tests for sensor_plausibility.h -- the water-step check (TODO 27).
//
// The rate vectors come from the fleet's own measured distributions
// (2026-08-01 analysis of the 2026-07-16 production capture): worst legitimate
// water 0.93 degC/h (gisebo-01 tank), worst enclosed 1.79 (gisebo-04 fridge),
// bench-in-sun air max 7.4 (gisebo-05). The threshold is 10 degC/h.

#include <cstdio>
#include <cmath>
#include "../../sensor_plausibility.h"

static int failures = 0;
static void check(bool ok, const char *what) {
  std::printf("  %-4s  %s\n", ok ? "ok" : "FAIL", what);
  if (!ok) failures = 1;
}

int main() {
  std::printf("\nsensor_plausibility -- water-step check\n");

  // ---- the measured legitimate cases must pass ----
  check(waterStepPlausible(16.8f, 16.8f + 0.93f, 3600),
        "worst production tank hour (0.93 degC/h) passes");
  check(waterStepPlausible(9.0f, 9.0f + 1.79f, 3600),
        "worst fridge hour (1.79 degC/h, compressor cycling) passes");
  check(waterStepPlausible(31.56f, 39.0f, 3600),
        "bench-in-sun worst hour (7.4 degC/h) passes -- no crying wolf on the test unit");

  // ---- the acute air cases must fail ----
  check(!waterStepPlausible(18.0f, 30.0f, 3600),
        "12 degC in an hour (probe surfaced into sun) fails");
  check(!waterStepPlausible(18.0f, 24.0f, 1800),
        "6 degC in 30 min (12 degC/h) fails");
  check(!waterStepPlausible(22.0f, 10.0f, 3600),
        "direction does not matter: -12 degC/h fails too");

  // ---- rate scales with the interval ----
  // Winter index 7 = 6 h wakes: seasonal water genuinely moves; 15 degC over
  // 6 h is 2.5 degC/h and must pass.
  check(waterStepPlausible(10.0f, 25.0f, 21600), "15 degC across a 6 h wake passes");
  // The same 15 degC over 30 min is 30 degC/h and must fail.
  check(!waterStepPlausible(10.0f, 25.0f, 1800), "15 degC across a 30 min wake fails");
  // 7-day wakes: essentially anything seasonal passes.
  check(waterStepPlausible(2.0f, 26.0f, 604800), "24 degC across a 7-day wake passes");

  // ---- the quantisation floor at short intervals ----
  // 1-min wake: the rate allowance alone would be 0.167 degC, under one 0.2
  // wire quantum. The 1.0 degC floor must absorb quantisation + ADC noise...
  check(waterStepPlausible(20.0f, 20.8f, 60), "0.8 degC step at a 1-min wake passes (floor)");
  check(waterStepPlausible(20.0f, 21.0f, 60), "exactly the 1.0 degC floor passes (inclusive)");
  // ...but a genuinely large step still fails even at 1 min.
  check(!waterStepPlausible(20.0f, 21.3f, 60), "1.3 degC step at a 1-min wake fails");

  // ---- no verdict without evidence ----
  check(waterStepPlausible(NAN, 20.0f, 3600), "NaN previous -> no verdict (true)");
  check(waterStepPlausible(20.0f, NAN, 3600), "NaN current -> no verdict (true)");
  check(waterStepPlausible(20.0f, 35.0f, 0),  "dt 0 (first sample after boot) -> no verdict (true)");

  // ---- boundary exactness ----
  // 10 degC/h for exactly one hour: allowed == delta, inclusive.
  check(waterStepPlausible(15.0f, 25.0f, 3600), "exactly 10 degC over exactly 1 h passes (inclusive)");
  check(!waterStepPlausible(15.0f, 25.1f, 3600), "10.1 degC over 1 h fails");

  std::printf("\n%s\n\n", failures ? "FAILED" : "all passed");
  return failures;
}
