#pragma once
//
// How a device decides its sampling interval, and what it adds to the payload.
//
// Two implementations:
//   PrimaryCellPolicy  -- 6 V primary pack, 9-byte payload,  FPorts 10/20
//   SolarPolicy        -- 1S2P li-ion + panel, 15-byte payload, FPorts 11/21
//
// Selected at boot by probing for the INA219 (sprint 03). One firmware image for
// every board -- the same reason latitude lives in the decoder and keys are
// derived from the silicon ID. There is no per-unit configuration anywhere in
// this system, and there must not be.
//
// The season machine sits OUTSIDE this interface (season.h). Both policies
// consume it; neither owns it. If a policy needs to reach inside it, the
// boundary is wrong.
//
#include <stdint.h>
#include "season.h"

// ---------------------------------------------------------------------------
// Voltage bands, with hysteresis
// ---------------------------------------------------------------------------
//
// A single SAMD21 ADC sample carries roughly +/-19 mV of noise (6.45 mV LSB
// through the A7 100k/100k divider). A pack sitting within that of a band edge
// flips its offset EVERY WAKE -- and on the solar variant the 3.85 V edge gates
// the solar bonus, so each flip swings the interval 5 min <-> 30 min.
//
// The season machine got 1 degC of hysteresis for exactly this reason. The
// voltage ladder never did.
//
// Rule: DEGRADE at the nominal edge, IMPROVE at nominal + hysteresis.
// Asymmetric on purpose -- react promptly to a failing pack, recover
// reluctantly. The protective response is never delayed.
//
// 50 mV comfortably exceeds the +/-19 mV noise band. Pair it with averaging the
// ADC (see getBatteryVoltage) -- averaging narrows the noise at source, the band
// absorbs what is left. They are complements, not alternatives.
#define VOLTAGE_HYST_V 0.05f

// edge[] is high-to-low: {healthy, low, critical}.
// Returns 0 (healthy) .. 3 (below critical).
//
// `prev` makes this LATCHED STATE, not a pure function of v. It must persist
// across wakes, and it belongs in .noinit beside the season state (sprint 03).
inline uint8_t voltageOffsetHyst(float v, uint8_t prev, const float edge[3]) {
  for (uint8_t i = 0; i < 3; i++) {
    // Improving INTO band i needs +hyst; holding band i only needs the nominal
    // edge. So a pack must climb 50 mV above the threshold to be believed.
    float e = edge[i] + ((prev > i) ? VOLTAGE_HYST_V : 0.0f);
    if (v >= e) return i;
  }
  return 3;
}

#define MAX_INTERVAL_INDEX 10
#define MIN_INTERVAL_INDEX 1   // index 0 is unused; never schedule it

inline uint8_t clampIntervalIndex(int idx, uint8_t floorIdx) {
  if (idx > MAX_INTERVAL_INDEX) return MAX_INTERVAL_INDEX;
  if (idx < floorIdx) return floorIdx;
  return (uint8_t)idx;
}

// ---------------------------------------------------------------------------
// The interface
// ---------------------------------------------------------------------------
//
// Three call-site contracts are load-bearing and were previously implicit.
// They are stated here because no single task could state them alone.
//
class PowerPolicy {
public:
  virtual ~PowerPolicy() {}

  // Called once from setup(), after the variant probe has chosen a policy.
  virtual void begin() = 0;

  // Called ONCE per wake, immediately after sensors.requestTemperatures(),
  // INSIDE the Dallas conversion window.
  //
  // Must return well under that window -- SolarPolicy spends ~68 ms on INA219
  // averaging. The CORE measures the window from requestTemperatures(), not from
  // this returning, so work done here SHRINKS the remaining wait rather than
  // extending the wake. Do not put a delay in here.
  virtual void onWake() = 0;

  // Called ONLY after EV_TXCOMPLETE, so the interval changes only after a
  // successful uplink. That invariant is what makes byte 0 mean "the interval
  // these six samples were taken at".
  //
  // vbat is read by the CORE, not the policy: the A7 divider is a board
  // property, and both policies interpret the same reading through different
  // bands. It is sampled at wake and BEFORE LMIC_setTxData2(), so it reads
  // essentially open-circuit -- cold sag under the 120 mA TX load never enters
  // the measurement.
  virtual uint8_t decideInterval(float tempC, float vbat) = 0;

  // Appends after byte 8. Returns bytes written: 0 for primary, 6 for solar.
  // Bytes 0-8 belong to the core and are byte-identical across variants.
  //
  // Returns the length rather than exposing a separate payloadLen(), so the two
  // cannot disagree.
  virtual uint8_t appendPayload(uint8_t *buf) = 0;

  // 10/20 primary, 11/21 solar. The variant is carried by the FPort, which is
  // what makes the probe-misdetect alarm possible at all: a solar device that
  // appears on FPort 10 failed to find its INA219 and is about to park itself
  // at a 7-day interval.
  virtual uint8_t fport(uint8_t runMode) = 0;
};
