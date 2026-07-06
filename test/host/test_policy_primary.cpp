// Host tests for PrimaryCellPolicy -- the interval ladder and its voltage
// hysteresis.
//
// Two jobs:
//   S02-14 -- pin today's behaviour, so the refactor provably changed nothing
//   S02-20 -- prove the hysteresis works under NOISE, not just clean steps
//
// The dithering test is the one that matters. A single-point test walks cleanly
// across a band edge and passes against the DEFECTIVE code -- the bug only
// appears when the input dithers, which is exactly what +/-19 mV of ADC noise
// does to a pack sitting on an edge.
//
// Build and run:  test/host/run_tests.sh

#include "../../policy_primary.h"
#include <cstdio>
#include <cmath>

static int failures = 0;

static void check(bool ok, const char *what) {
  if (ok) { std::printf("  ok    %s\n", what); }
  else    { std::printf("  FAIL  %s\n", what); failures++; }
}

// Fresh policy in a known season, so ladder tests are not order-dependent.
static PrimaryCellPolicy makePolicy(uint8_t season) {
  PrimaryCellPolicy p;
  p.begin();
  p.seasonState_ = season;
  return p;
}

int main() {
  std::printf("\npolicy_primary -- interval ladder and voltage hysteresis\n");

  // -------------------------------------------------------------------------
  // 1. Season bases with a healthy pack. Unchanged from the original.
  // -------------------------------------------------------------------------
  {
    auto s = makePolicy(SEASON_SUMMER);
    check(s.decideInterval(20.0f, 6.0f) == 4, "SUMMER + healthy -> index 4 (30 min)");
    auto m = makePolicy(SEASON_MID);
    check(m.decideInterval(10.0f, 6.0f) == 5, "MID + healthy -> index 5 (60 min)");
    auto w = makePolicy(SEASON_WINTER);
    check(w.decideInterval(2.0f, 6.0f) == 7, "WINTER + healthy -> index 7 (6 h)");
  }

  // -------------------------------------------------------------------------
  // 2. Voltage offsets add to the base. Fresh policy each time so the latched
  //    voltage state starts healthy and only degrades.
  // -------------------------------------------------------------------------
  {
    auto a = makePolicy(SEASON_SUMMER);
    check(a.decideInterval(20.0f, 5.0f) == 4, "5.0V (>= healthy) -> offset 0 -> index 4");
    auto b = makePolicy(SEASON_SUMMER);
    check(b.decideInterval(20.0f, 4.9f) == 5, "4.9V -> offset 1 -> index 5");
    auto c = makePolicy(SEASON_SUMMER);
    check(c.decideInterval(20.0f, 4.2f) == 6, "4.2V -> offset 2 -> index 6");
    auto d = makePolicy(SEASON_SUMMER);
    check(d.decideInterval(20.0f, 3.4f) == 7, "3.4V (< critical) -> offset 3 -> index 7");
  }

  // -------------------------------------------------------------------------
  // 3. The clamp. Winter + critical would be 7 + 3 = 10, which is exactly the
  //    ceiling. This is the memory-crash edge case the original guarded.
  // -------------------------------------------------------------------------
  {
    auto w = makePolicy(SEASON_WINTER);
    check(w.decideInterval(2.0f, 3.0f) == 10, "WINTER + below-critical -> 10, not 11");
  }

  // -------------------------------------------------------------------------
  // 4. THE TEST THAT MATTERS (S02-20).
  //
  //    Hold the pack exactly on a band edge and add +/-19 mV of ADC noise.
  //    Without hysteresis the offset flips every wake. With it, nothing moves.
  //
  //    A clean-step test cannot see this defect. Only a dithering one can.
  // -------------------------------------------------------------------------
  {
    auto p = makePolicy(SEASON_SUMMER);
    p.decideInterval(20.0f, 6.0f);        // settle healthy

    int changes = 0;
    uint8_t last = p.voltageState_;
    for (int i = 0; i < 200; i++) {
      float noise = 0.019f * std::sin(i * 1.7f);   // deterministic +/-19 mV
      p.decideInterval(20.0f, VOLTAGE_HEALTHY_V + noise);
      if (p.voltageState_ != last) changes++;
      last = p.voltageState_;
    }
    // AT MOST ONE transition -- not zero. The asymmetric rule degrades at the
    // nominal edge, so the first noise dip below 5.000 trips it to offset 1.
    // It then LATCHES: recovery needs 5.05, which noise alone cannot reach.
    // That is the anti-flapping property. Zero transitions would require
    // symmetric hysteresis (degrade at 4.95), which would delay the protective
    // response -- deliberately not what we chose.
    //
    // My first draft asserted changes == 0 and failed. The code was right.
    check(changes <= 1,
          "dither at 5.000V +/-19mV for 200 wakes: at most ONE transition, then latches");
    check(p.voltageState_ == 1,
          "dither at 5.000V: settles degraded and STAYS there (no oscillation)");
  }
  {
    // Same, on the way down. A pack that has already degraded to offset 1 and
    // sits on the edge must not flap back and forth either.
    auto p = makePolicy(SEASON_SUMMER);
    p.decideInterval(20.0f, 4.0f);        // settle at offset 2
    uint8_t settled = p.voltageState_;

    int changes = 0;
    for (int i = 0; i < 200; i++) {
      float noise = 0.019f * std::sin(i * 2.3f);
      p.decideInterval(20.0f, VOLTAGE_LOW_V + noise);
      if (p.voltageState_ != settled) changes++;
    }
    // Already degraded past this edge, so noise around it cannot move anything:
    // improving would need 4.35, which +/-19 mV cannot reach.
    check(changes == 0, "dither at 4.300V +/-19mV: already-degraded offset holds, zero flips");
  }

  // -------------------------------------------------------------------------
  // 5. The asymmetry. Degrade promptly, recover reluctantly -- the protective
  //    response is never delayed, but a pack must EARN its way back.
  // -------------------------------------------------------------------------
  {
    auto p = makePolicy(SEASON_SUMMER);
    p.decideInterval(20.0f, 6.0f);
    check(p.voltageState_ == 0, "start healthy");

    p.decideInterval(20.0f, 4.99f);
    check(p.voltageState_ == 1, "falls to offset 1 immediately below 5.00 (prompt)");

    p.decideInterval(20.0f, 5.02f);
    check(p.voltageState_ == 1, "5.02 does NOT restore healthy (needs 5.00 + 50 mV)");

    p.decideInterval(20.0f, 5.05f);
    check(p.voltageState_ == 0, "5.05 restores healthy (nominal + hysteresis)");
  }

  // -------------------------------------------------------------------------
  // 6. A collapsing pack must reach offset 3 in ONE call.
  //    Unlike the season machine, the voltage ladder is NOT a stepping machine:
  //    a failing pack should not need three wakes to be recognised.
  // -------------------------------------------------------------------------
  {
    auto p = makePolicy(SEASON_SUMMER);
    p.decideInterval(20.0f, 6.0f);
    check(p.voltageState_ == 0, "healthy");
    p.decideInterval(20.0f, 3.0f);
    check(p.voltageState_ == 3, "collapse 6.0V -> 3.0V reaches offset 3 in ONE call");
  }

  // -------------------------------------------------------------------------
  // 7. Production anchors. Real values from the 2026-07-17 capture.
  // -------------------------------------------------------------------------
  {
    auto p = makePolicy(SEASON_SUMMER);
    check(p.decideInterval(16.8f, 5.768f) == 4,
          "production: gisebo-01 at 16.8C / 5.768V -> index 4 (matches byte 0)");
  }
  {
    // gisebo-04's fridge conditions, had it run this firmware. 9.0 C is
    // Fall/Spring; 5.233 V is comfortably healthy.
    auto p = makePolicy(SEASON_MID);
    check(p.decideInterval(9.0f, 5.233f) == 5,
          "production: gisebo-04 conditions (9.0C / 5.233V) -> index 5");
  }

  // -------------------------------------------------------------------------
  // 8. Payload and FPort.
  // -------------------------------------------------------------------------
  {
    auto p = makePolicy(SEASON_SUMMER);
    uint8_t buf[16] = {0};
    check(p.appendPayload(buf) == 0, "primary appends 0 bytes (9-byte payload)");
    check(p.fport(0) == 10, "PROD -> FPort 10");
    check(p.fport(1) == 20, "DEV -> FPort 20");
  }

  std::printf("\n%s\n\n", failures ? "FAILED" : "all passed");
  return failures ? 1 : 0;
}
