#pragma once
//
// The solar signal: is the sun usable, and how much energy have we harvested.
//
// Pure logic, no Arduino dependencies, so the host tests exercise the same code
// the firmware runs. The INA219 I2C read is in the .ino; this is what we do with
// the numbers.
//
// ---------------------------------------------------------------------------
// Why sun-present needs TWO arms (voltage alone failed on real hardware)
// ---------------------------------------------------------------------------
//
// The original design keyed on bus voltage alone, sun_present = (bus > 3000 mV),
// reasoning that current cannot be trusted: when the pack is full the charger
// terminates and current collapses to ~0 in full summer sun. That half was
// right -- charge termination was OBSERVED (boot 2026-07-28 14:05: 0 mA with
// the bus at panel Voc, 5.10 V, battery 4.16 V).
//
// The voltage half was wrong, and the first honest night proved it
// (2026-07-28/29, ttn-captures/gisebo05-ttn-20260729-morning.jsonl): at night
// the bus does NOT sit near 0 V. The charger input node back-feeds from the
// pack, and the bus read battery - ~180 mV (~3.57-3.61 V) from dusk to dawn.
// A li-ion pack cannot go below ~3.4 V with the Feather alive, so an absolute
// 3000 mV floor was STRUCTURALLY unreachable: sunPresent() held true through
// eight hours of darkness and the EWMA rose 0.255 -> 0.529 overnight -- the
// same poisoning as the frozen-sensor defect, from an honest sensor.
//
// The measured table also kills the two one-arm fixes:
//   * absolute threshold above 4.2 V: misses every observed charging point
//     (bus 3.57-3.99 V while current flows)
//   * pure relative (bus > battery + margin): fails low light -- 18:07 had
//     11.6 mA FLOWING with the bus 90 mV BELOW the battery, and dawn charging
//     runs ~180 mV below
//
// So: EITHER arm proves sun.
//   current arm   -- covers every charging case, including bus-below-battery
//                    low light (11.6, 2.4, 1-2 mA all measured)
//   relative arm  -- covers the one case current cannot: charge termination,
//                    0 mA by design with the bus at Voc (+940 mV over battery)
// Night fails both: 0 mA, bus ~180 mV BELOW battery.
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

// Current arm: any measurable charge current proves sun. 1 mA sits well above
// the INA219's noise floor (0.1 mA/LSB at our calibration) and well below the
// weakest charging ever observed (1-2 mA at dawn/dusk).
#define SUN_CURRENT_MA 1.0f

// Relative arm: bus meaningfully ABOVE the battery proves the panel is driving
// the node. 150 mV sits between the tightest observed night offset (bus at
// battery MINUS 169 mV, dusk) and charge termination (bus at battery + 940 mV).
// The margin does not need to cover active charging (+122/+9/-90 mV observed)
// -- the current arm owns those.
#define SUN_BUS_ABOVE_BATT_MV 150u

// EWMA time constant: 24 h. Long enough to average a full day/night cycle, short
// enough to track weather over a few days.
#define SUN_EWMA_TAU_S 86400.0f

// Either arm proves sun; night fails both. The battery reading comes from the
// same wake's A7 measurement, so both voltages see the same conditions.
//
// SOLAR_NO_INA219 bench note: with no shunt the caller passes currentMa = 0, so
// the current arm is always false and the relative arm alone decides -- which
// works on the bench divider, where the node genuinely reads ~0 V at night.
inline bool sunPresent(uint16_t busMillivolts, float currentMa,
                       uint16_t batteryMillivolts) {
  if (currentMa >= SUN_CURRENT_MA) return true;
  return busMillivolts > (uint32_t)batteryMillivolts + SUN_BUS_ABOVE_BATT_MV;
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
