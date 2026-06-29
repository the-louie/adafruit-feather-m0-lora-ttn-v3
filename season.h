#pragma once
//
// Seasonal baseline for the sampling interval, from water temperature.
//
// SHARED between both power policies. Neither owns it -- they consume it and
// differ only in their voltage bands and what they add on top. If a policy ever
// needs to reach inside this, the boundary is wrong.
//
// Pure logic, no Arduino dependencies, so the host tests exercise the same code
// the firmware runs.
//
// ---------------------------------------------------------------------------
// Why water temperature and not day length
// ---------------------------------------------------------------------------
//
// The season machine exists to sample less when the tank changes slowly.
// Water temperature reads that DIRECTLY. Day length was considered and rejected:
// it is an astronomical proxy for weather, which is a proxy for the tank -- two
// steps removed, and wrong during an unseasonable warm spell.
//
// ---------------------------------------------------------------------------
// Why voltage must never drive this
// ---------------------------------------------------------------------------
//
// A pack reads LOW when it is cold. Measured in production: gisebo-01's battery
// tracks temperature at +12.8 mV/degC (r = 0.93), which extrapolates to ~321 mV
// of drift across a 25 degC season AT CONSTANT CHARGE. If voltage drove the
// season, a cold snap would look like a flat battery and lock the device into a
// long interval that a warm spell could never undo. It does not, and that is
// deliberate.
//
#include <stdint.h>

enum SeasonState : uint8_t {
  SEASON_WINTER = 0,
  SEASON_MID    = 1,  // Fall / Spring
  SEASON_SUMMER = 2,
};

// 1 degC of hysteresis on each boundary, to stop the state flapping when the
// water sits exactly on an edge. ENTER is the threshold to move UP a season;
// LEAVE is the threshold to fall back.
#define TEMP_SUMMER_ENTER_C 16.0f
#define TEMP_SUMMER_LEAVE_C 15.0f
#define TEMP_MID_ENTER_C     8.0f
#define TEMP_MID_LEAVE_C     7.0f

// Interval table indices. See kIntervalSecondsByIndex in the sketch.
#define BASE_INDEX_SUMMER 4  // 30 min
#define BASE_INDEX_MID    5  // 60 min
#define BASE_INDEX_WINTER 7  // 6 h

// A DS18B20 that has lost its sensor returns -127.0 (DEVICE_DISCONNECTED_C),
// and a failed read can give NaN. Both must leave the season untouched rather
// than dragging it to Winter.
inline bool seasonTempValid(float tempC) {
  if (tempC != tempC) return false;                  // NaN
  return tempC >= -50.0f && tempC <= 60.0f;
}

// Advance the season by AT MOST ONE level per call.
//
// This is deliberate and inherited from the original else-if chain: a jump from
// Summer to 5 degC water reaches Fall/Spring on one call and Winter only on the
// next. Since this is called once per successful uplink, a cold-start in winter
// takes two uplinks to settle -- expect a rebooted device to transmit sooner
// than steady state for a short while.
inline uint8_t seasonUpdate(uint8_t state, float tempC) {
  if (!seasonTempValid(tempC)) {
    return state;  // keep the previous state; do not guess
  }
  if (state == SEASON_SUMMER && tempC < TEMP_SUMMER_LEAVE_C) return SEASON_MID;
  if (state == SEASON_MID    && tempC >= TEMP_SUMMER_ENTER_C) return SEASON_SUMMER;
  if (state == SEASON_MID    && tempC < TEMP_MID_LEAVE_C)     return SEASON_WINTER;
  if (state == SEASON_WINTER && tempC >= TEMP_MID_ENTER_C)    return SEASON_MID;
  return state;
}

inline uint8_t seasonBaseIndex(uint8_t state) {
  if (state == SEASON_SUMMER) return BASE_INDEX_SUMMER;
  if (state == SEASON_MID)    return BASE_INDEX_MID;
  return BASE_INDEX_WINTER;
}
