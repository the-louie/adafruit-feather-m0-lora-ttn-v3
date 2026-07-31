#pragma once
//
// Is a temperature STEP physically consistent with a submerged sensor?
//
// The failure this exists to catch is the one every defect in this project has
// shared: a reading that is valid, plausible and WRONG. A sensor lifted out of
// the water (lake level, failed mount, pulled cable) keeps producing perfectly
// well-formed numbers -- they just measure air. The fault frame's other bits
// cannot see that; this can, because water's thermal mass forbids fast change.
//
// Thresholds are DERIVED FROM FLEET DATA (2026-08-01), not guessed:
//
//   gisebo-01, production tank, 107 uplinks:   max observed  0.93 degC/h
//   gisebo-04, fridge (compressor cycling):    max observed  1.79 degC/h
//   gisebo-05, bench in direct sun (air):      max observed  7.4  degC/h
//
// 10 degC/h sits 5.6x above the worst legitimate water rate ever recorded by
// this fleet, ABOVE the bench-in-sun maximum (so a test unit in air does not
// cry wolf hourly), and far below what a dark probe surfaced into direct sun
// does. This makes the check a detector for the ACUTE cases -- surfacing into
// sun, day/night air swings, gross read glitches -- and deliberately NOT a
// detector for gentle drift; a probe hanging in still shaded air tracks slowly
// enough to pass. Partial coverage that never false-alarms beats sensitive
// coverage that gets ignored.
//
// The rate scales with the interval (1 min .. 7 days), with a floor so the
// 0.2 degC wire quantum and ADC noise cannot trip short intervals: at 1 min the
// rate-derived allowance would be 0.17 degC, below one quantisation step.
//
// Known non-coverage, stated: a sensor frozen INTO ice pins near 0 degC and
// stops varying -- flat, not fast -- so this check cannot see it. That case
// needs seasonal reasoning (backend, item 10 territory).
//
// Pure logic, no Arduino dependencies, host-tested (test_sensor_plausibility).
//
#include <stdint.h>

#define TEMP_MAX_RATE_C_PER_H 10.0f
#define TEMP_STEP_FLOOR_C      1.0f

// True if moving prevC -> nowC over dtSeconds is consistent with water.
// Invalid inputs (NaN from a failed read) and dt 0 (the first sample after a
// boot, which slept for nothing) yield true: no comparison, no verdict --
// absence of evidence must not raise a fault.
inline bool waterStepPlausible(float prevC, float nowC, uint32_t dtSeconds) {
  if (prevC != prevC || nowC != nowC) return true;   // NaN: no verdict
  if (dtSeconds == 0) return true;                   // no elapsed time: no verdict
  float allowed = TEMP_MAX_RATE_C_PER_H * ((float)dtSeconds / 3600.0f);
  if (allowed < TEMP_STEP_FLOOR_C) allowed = TEMP_STEP_FLOOR_C;
  float d = nowC - prevC;
  if (d < 0) d = -d;
  return d <= allowed;
}
