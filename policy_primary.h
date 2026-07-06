#pragma once
//
// PrimaryCellPolicy -- the 6 V non-rechargeable pack (4xAA, or 2x 3 V lithium).
//
// This is today's proven algorithm, moved. Production confirms it works:
// gisebo-01 sits at interval index 4, which is exactly Summer base (16.8 degC
// water) + voltage_offset 0 (5.768 V). Do not "improve" it while moving it.
//
// The ONE deliberate change is voltage-band hysteresis -- see power_policy.h.
//
// 9-byte payload (nothing appended), FPorts 10/20.
//
// ---------------------------------------------------------------------------
// A note on what this policy is actually for
// ---------------------------------------------------------------------------
//
// After the cutover, no deployed device runs it: gisebo-01 is retired,
// gisebo-04 stays on V5 forever, and gisebo-05 is solar. It exists as
// optionality for future non-rechargeable builds.
//
// Worth knowing honestly: its interval ladder is not really an ENERGY strategy.
// From the project's own figures a 12x interval change (30 min -> 6 h) moves
// consumption 31%, which backs out to ~6.9 mAh/day (~290 uA) drawn regardless of
// wake count -- consistent with a stock Feather M0's regulator, charger and USB
// leak. So the ladder saves under 4%, and the real value here is a
// chemistry-appropriate threshold set plus a proven sampling cadence. Both are
// good reasons to keep it. Neither is the reason written on the tin.
//
#include "power_policy.h"

// 6 V pack bands. Alkaline slopes steadily through all three; 2x 3 V lithium
// sits flat above 5.0 V for most of its life then falls off a cliff.
//
// Production telemetry distinguishes the two without opening a box: gisebo-01
// reads +12.8 mV/degC (alkaline's temperature coefficient), while gisebo-04 on
// lithium in a fridge shows no correlation and drifts -2.5 mV/day at 9 degC.
#define VOLTAGE_HEALTHY_V  5.0f
#define VOLTAGE_LOW_V      4.3f
#define VOLTAGE_CRITICAL_V 3.5f

class PrimaryCellPolicy : public PowerPolicy {
public:
  void begin() override {
    // Start at Summer. A cold start in winter therefore takes two uplinks to
    // settle, because the season machine steps one level per call -- expect a
    // rebooted device to transmit sooner than steady state for a short while.
    seasonState_ = SEASON_SUMMER;
    voltageState_ = 0;  // assume healthy until a reading says otherwise
  }

  // Nothing to sample. The battery is read by the core; there is no INA219.
  void onWake() override {}

  uint8_t decideInterval(float tempC, float vbat) override {
    seasonState_ = seasonUpdate(seasonState_, tempC);

    static const float edges[3] = {VOLTAGE_HEALTHY_V, VOLTAGE_LOW_V, VOLTAGE_CRITICAL_V};
    voltageState_ = voltageOffsetHyst(vbat, voltageState_, edges);

    int idx = (int)seasonBaseIndex(seasonState_) + (int)voltageState_;
    // Floor at 1: index 0 is unused, so a device must never be scheduled with it.
    // Ceiling at 10 catches the memory-crash edge case (winter + critical).
    return clampIntervalIndex(idx, MIN_INTERVAL_INDEX);
  }

  // 9-byte payload -- the core owns all of it.
  uint8_t appendPayload(uint8_t *buf) override {
    (void)buf;
    return 0;
  }

  uint8_t fport(uint8_t runMode) override { return runMode == 0 ? 10 : 20; }

  // Exposed for .noinit persistence (sprint 03) and for the host tests.
  uint8_t seasonState_ = SEASON_SUMMER;
  uint8_t voltageState_ = 0;
};
