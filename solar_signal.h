#pragma once
//
// The solar signal: is the sun usable, and how much energy have we harvested.
//
// Pure logic, no Arduino dependencies, so the host tests exercise the same code
// the firmware runs. The INA219 I2C read is in the .ino; this is what we do with
// the numbers.
//
// ---------------------------------------------------------------------------
// Why bus VOLTAGE, not current
// ---------------------------------------------------------------------------
//
// The INA219 sits in the charging path and measures HARVESTED current, not
// available sunlight. When the pack is full the charger terminates and current
// collapses to ~0 -- in full summer sun. With the claimed surplus that is most
// of the summer. So a full pack in bright sun looks identical to darkness if you
// read current.
//
// Panel BUS VOLTAGE does not have this problem: at night the panel is dark, the
// Schottky blocks, and the bus sits near 0 V; whenever there is sun the bus reads
// either the charger's operating point (~4.5-5 V) or, once charge terminates, the
// panel's open-circuit voltage. So sun_present = (bus_mV > threshold).
//
// ---------------------------------------------------------------------------
// Why the window is TIME-based, not wake-based
// ---------------------------------------------------------------------------
//
// A window of N wakes is a window of N x interval seconds -- and the interval is
// what the window controls. A sunny afternoon shortens the interval, which
// shrinks the window to a few hours that are still all daylight, so it never sees
// the night that should pull the average down. It hunts on a multi-day period.
//
// The fix is to decay against REAL ELAPSED TIME from the RTC, so the time
// constant is independent of the sampling rate.
//
#include <stdint.h>
#include <math.h>

// Bus voltage above this means the panel is lit. 3000 mV sits well above the
// night level (~0 V) and below any daytime operating point.
#define SUN_PRESENT_MV 3000

// EWMA time constant: 24 h. Long enough to average a full day/night cycle, short
// enough to track weather over a few days.
#define SUN_EWMA_TAU_S 86400.0f

inline bool sunPresent(uint16_t busMillivolts) {
  return busMillivolts > SUN_PRESENT_MV;
}

// Decay the sun-presence EWMA by real elapsed seconds.
//
//   alpha = 1 - exp(-dt / TAU)
//   ewma += alpha * (present - ewma)
//
// Do NOT substitute the linear approximation alpha ~= dt/TAU. It errs ~13% at a
// 6 h interval -- exactly the winter case, where dt is largest.
inline float sunEwmaUpdate(float ewma, bool present, uint32_t dtSeconds) {
  if (dtSeconds == 0) return ewma;   // no time passed, no change
  float alpha = 1.0f - expf(-(float)dtSeconds / SUN_EWMA_TAU_S);
  float target = present ? 1.0f : 0.0f;
  return ewma + alpha * (target - ewma);
}

// ---------------------------------------------------------------------------
// Sun bonus gate, with hysteresis
// ---------------------------------------------------------------------------
//
// The EWMA ripples +/-0.11 daily against its 24 h time constant, and Fall/Spring
// peaks at 0.522 every afternoon. A bare 0.5 threshold would flap. Engage at 0.55,
// release at 0.45 -- a band centred on the intended 0.5, so summer (min ~0.53)
// stays ON and fall (max ~0.52) stays OFF. Same idea as the season machine's
// 1 degC hysteresis.
#define SUN_BONUS_ENGAGE 0.55f
#define SUN_BONUS_RELEASE 0.45f

// Latched: pass the previous state in, get the new one out.
inline bool sunBonusActive(float ewma, bool wasActive) {
  if (wasActive) return ewma >= SUN_BONUS_RELEASE;   // stay on until it drops below release
  return ewma >= SUN_BONUS_ENGAGE;                   // turn on only above engage
}

// ---------------------------------------------------------------------------
// Harvest accumulator
// ---------------------------------------------------------------------------
//
// Integrated mAh since cold boot. 16-bit at 1 mAh/LSB, so it WRAPS: at energy
// balance harvest ~= consumption ~= 7-28 mAh/day, so 65535 mAh is 6.4 years at
// the high end -- inside the 5-10 year replacement window. The backend unwraps
// (a wrap decreases the value, and uplinks are frequent enough that no wrap is
// missed). Do NOT coarsen the LSB to avoid the wrap: at 10 mAh/LSB a day's
// harvest is 1-3 LSB, which destroys the daily resolution the pack-health trend
// needs.
//
// Accumulate charge in a float milliamp-hour accumulator and hand out whole mAh,
// so sub-mAh wakes are not lost to rounding.
struct HarvestAccumulator {
  float pendingMah;       // sub-mAh remainder not yet counted
  uint16_t totalMah;      // wraps
};

inline void harvestInit(HarvestAccumulator *h) {
  h->pendingMah = 0.0f;
  h->totalMah = 0;
}

// Add one wake's charge. currentMa over dtSeconds -> mAh = mA * (dt/3600).
inline void harvestAdd(HarvestAccumulator *h, float currentMa, uint32_t dtSeconds) {
  h->pendingMah += currentMa * ((float)dtSeconds / 3600.0f);
  // Move whole mAh into the (wrapping) total.
  while (h->pendingMah >= 1.0f) {
    h->pendingMah -= 1.0f;
    h->totalMah = (uint16_t)(h->totalMah + 1);   // wraps naturally
  }
}
