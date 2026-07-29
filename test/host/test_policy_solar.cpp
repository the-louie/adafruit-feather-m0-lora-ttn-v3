// Host tests for SolarPolicy -- interval ladder, the two-gate bonus, the
// self-correcting loop, and the 15-byte payload.
//
// The self-correction test (5) is the one that de-risks the unmeasured index-2
// floor: if aggressive sampling outruns harvest, the pack drains, the bonus
// drops, and the interval returns to baseline on its own.
//
// Build and run:  test/host/run_tests.sh

#include "../../policy_solar.h"
#include <cstdio>
#include <cmath>

static int failures = 0;

static void check(bool ok, const char *what) {
  if (ok) { std::printf("  ok    %s\n", what); }
  else    { std::printf("  FAIL  %s\n", what); failures++; }
}

// A policy with the EWMA already saturated one way or the other, so interval
// tests do not have to warm it up.
static SolarPolicy makeSolar(uint8_t season, bool sunny) {
  SolarPolicy p;
  p.begin();
  p.seasonState_ = season;
  p.ewma_ = sunny ? 1.0f : 0.0f;
  p.bonusActive_ = sunny;   // pretend it has been latched
  return p;
}

int main() {
  std::printf("\npolicy_solar -- interval ladder and payload\n");

  // -------------------------------------------------------------------------
  // 1. FPorts and li-ion bands.
  // -------------------------------------------------------------------------
  {
    SolarPolicy p; p.begin();
    check(p.fport(0) == 11, "PROD -> FPort 11");
    check(p.fport(1) == 21, "DEV -> FPort 21");
  }

  // -------------------------------------------------------------------------
  // 2. The bonus. Healthy pack + latched sun -> floor. Summer 4 - 2 = 2.
  // -------------------------------------------------------------------------
  {
    auto p = makeSolar(SEASON_SUMMER, true);
    check(p.decideInterval(20.0f, 4.0f) == 2, "SUMMER + healthy + sun -> index 2 (floor)");
    auto f = makeSolar(SEASON_MID, true);
    check(f.decideInterval(10.0f, 4.0f) == 3, "FALL + healthy + sun -> index 3");
    auto w = makeSolar(SEASON_WINTER, true);
    check(w.decideInterval(2.0f, 4.0f) == 5, "WINTER + healthy + sun -> index 5");
  }

  // -------------------------------------------------------------------------
  // 3. Both gates required. Sun without a healthy pack gives NO bonus -- the
  //    signals must not fight.
  // -------------------------------------------------------------------------
  {
    // Healthy li-ion is >= 3.85. 3.70 V is offset 1, so no bonus even in full sun.
    auto p = makeSolar(SEASON_SUMMER, true);
    check(p.decideInterval(20.0f, 3.70f) == 5,
          "SUMMER + sun but offset 1 (3.70 V) -> base 4 + 1, NO bonus -> 5");

    // Healthy pack but no sun -> also no bonus.
    auto q = makeSolar(SEASON_SUMMER, false);
    check(q.decideInterval(20.0f, 4.0f) == 4,
          "SUMMER + healthy but no sun -> base 4, NO bonus");
  }

  // -------------------------------------------------------------------------
  // 4. Floor clamp. The bonus can never push below index 2.
  // -------------------------------------------------------------------------
  {
    auto p = makeSolar(SEASON_SUMMER, true);
    // Summer base 4 - 2 = 2, already the floor; a hypothetical deeper base still
    // clamps. Confirm 2 is the minimum.
    check(p.decideInterval(20.0f, 4.0f) >= SOLAR_FLOOR_INDEX, "never below the floor");
  }

  // -------------------------------------------------------------------------
  // 5. THE SELF-CORRECTING LOOP. Simulate aggressive sampling draining the pack.
  //    As vbat falls past 3.85, the bonus must drop and the interval return to
  //    baseline WITHOUT any explicit "back off" logic.
  // -------------------------------------------------------------------------
  {
    auto p = makeSolar(SEASON_SUMMER, true);
    // Full pack: bonus engaged, at the floor.
    check(p.decideInterval(20.0f, 4.10f) == 2, "full pack -> floor (bonus on)");
    // Pack drains to the plateau but still healthy (>= 3.85): still bonus.
    check(p.decideInterval(20.0f, 3.90f) == 2, "3.90 V still healthy -> still floor");
    // Drains past the healthy edge (with hysteresis, 3.85 exact degrades):
    check(p.decideInterval(20.0f, 3.84f) == 5, "3.84 V -> offset 1, bonus gate closes -> baseline 5");
    // Even though the sun is still shining, the interval backed off on its own.
    check(p.ewma_ >= 0.9f, "  (sun EWMA is still high -- it was the PACK that backed us off)");
  }

  // -------------------------------------------------------------------------
  // 6. ingestSample drives the EWMA, harvest and latched bonus together.
  // -------------------------------------------------------------------------
  {
    SolarPolicy p; p.begin();
    check(p.ewma_ == 0.0f && !p.bonusActive_, "starts dark, bonus off");
    // Feed many sunny samples: EWMA climbs, bonus eventually latches.
    for (int i = 0; i < 200; i++) p.ingestSample(4700, 25.0f, 3700, 1800);  // 30-min sunny wakes
    check(p.ewma_ > 0.9f, "sustained sun -> EWMA climbs above 0.9");
    check(p.bonusActive_, "sustained sun -> bonus latches on");
    check(p.harvest_.totalMah > 0, "harvest accumulates from the current");

    // Now go dark -- with the REAL night signature (bus back-feeds to
    // battery - ~180 mV, NOT 0 V; measured 2026-07-28/29). Under the old
    // absolute threshold this exact input could never read as dark.
    for (int i = 0; i < 200; i++) p.ingestSample(3570, 0.0f, 3750, 1800);
    check(p.ewma_ < 0.45f, "sustained dark -> EWMA falls below the release threshold");
    check(!p.bonusActive_, "sustained dark -> bonus releases");
  }

  // -------------------------------------------------------------------------
  // 7. The charge-terminated case: current ~0 with a HIGH bus voltage must read
  //    as sunny, not dark. This is the case the whole signal design exists for.
  // -------------------------------------------------------------------------
  {
    SolarPolicy p; p.begin();
    p.ewma_ = 0.6f;   // recently sunny
    // Full pack in bright sun: charger terminated, current 0, bus at panel Voc.
    p.ingestSample(6000, 0.0f, 4100, 1800);
    check(sunPresent(6000, 0.0f, 4100), "Voc bus with 0 mA still reads sun_present (relative arm)");
    check(p.ewma_ >= 0.6f, "charge-terminated sample keeps the EWMA up (not dragged to 0)");
  }

  // -------------------------------------------------------------------------
  // 8. Payload append: 6 bytes, correct encodings.
  // -------------------------------------------------------------------------
  {
    SolarPolicy p; p.begin();
    p.lastBusMv_ = 4800;      // 4800/30 = 160
    p.lastCurrentMa_ = 25.0f; // 25/0.5 = 50
    p.ewma_ = 0.6f;           // 0.6*255 = 153
    p.harvest_.totalMah = 0x0102;
    p.bootCounter_ = 3;
    p.statusFlags_ = STATUS_CLOCK_VALID;
    p.bonusActive_ = true;

    uint8_t buf[8] = {0};
    uint8_t n = p.appendPayload(buf);
    check(n == 6, "appends exactly 6 bytes");
    check(buf[0] == 160, "byte 9: panel V 4800 mV / 30 = 160");
    check(buf[1] == 50,  "byte 10: panel I 25 mA / 0.5 = 50");
    check(buf[2] == 153, "byte 11: EWMA 0.6 -> 153");
    check(buf[3] == 0x01 && buf[4] == 0x02, "bytes 12-13: harvest 0x0102 big-endian");
    // status: bootCounter 3 in high bits (3<<5 = 0x60) | clock valid (0x04) | bonus (0x08)
    check(buf[5] == (0x60 | STATUS_CLOCK_VALID | STATUS_BONUS_ACTIVE),
          "byte 14: boot counter + clock-valid + bonus-active flags");
  }

  // -------------------------------------------------------------------------
  // 9. Payload clamps rather than wrapping.
  // -------------------------------------------------------------------------
  {
    SolarPolicy p; p.begin();
    p.lastBusMv_ = 60000;      // 60000/30 = 2000 -> clamp 255
    p.lastCurrentMa_ = 200.0f; // 200/0.5 = 400 -> clamp 255
    uint8_t buf[8] = {0};
    p.appendPayload(buf);
    check(buf[0] == 255, "panel V clamps at 255, does not wrap");
    check(buf[1] == 255, "panel I clamps at 255, does not wrap");
  }

  // -------------------------------------------------------------------------
  // 10. dt == 0 changes nothing -- the contract the .ino relies on for the
  //     FIRST sample after a boot, which has not slept at all.
  //
  //     setup() fills sleepIntervalSeconds from the restored interval index
  //     before any sleep happens, so passing it would fabricate elapsed time.
  //     Negligible for the EWMA, but harvest integrates current*dt directly: a
  //     warm reset at index 5 in sun would credit ~34 mAh that never flowed,
  //     more than a typical day's harvest, on top of the .noinit-restored
  //     total. The .ino passes 0 instead; this pins the behaviour that makes
  //     that safe. See TODO 22.
  // -------------------------------------------------------------------------
  {
    SolarPolicy p; p.begin();
    for (int i = 0; i < 20; i++) p.ingestSample(4700, 25.0f, 3700, 3600);  // build real state
    const float    ewmaBefore    = p.ewma_;
    const uint16_t harvestBefore = p.harvest_.totalMah;
    const bool     bonusBefore   = p.bonusActive_;

    p.ingestSample(4700, 25.0f, 3700, 0);   // a "sample" covering no elapsed time

    check(p.ewma_ == ewmaBefore, "dt=0: EWMA unchanged");
    check(p.harvest_.totalMah == harvestBefore, "dt=0: harvest total unchanged");
    check(p.bonusActive_ == bonusBefore, "dt=0: latched bonus unchanged");

    // ...and it must not quietly bank a sub-mAh remainder either, or repeated
    // boots would still drift the accumulator upward one fraction at a time.
    for (int i = 0; i < 50; i++) p.ingestSample(4700, 25.0f, 3700, 0);
    check(p.harvest_.totalMah == harvestBefore, "dt=0 x50: harvest still unchanged");

    // The live readings must still land, so the payload/verbose frame reports
    // the panel state from a boot's first read even though it credits no time.
    p.ingestSample(3300, 7.5f, 3700, 0);
    check(p.lastBusMv_ == 3300, "dt=0: bus voltage still recorded");
    check(p.lastCurrentMa_ == 7.5f, "dt=0: current still recorded");
  }

  std::printf("\n%s\n\n", failures ? "FAILED" : "all passed");
  return failures ? 1 : 0;
}
