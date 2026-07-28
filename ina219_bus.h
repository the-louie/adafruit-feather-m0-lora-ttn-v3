#pragma once
//
// INA219 bus-voltage register (02h) interpretation: the CNVR/OVF status flags
// and the voltage math.
//
// The Adafruit accessor cannot be used for this: getBusVoltage_raw() returns
// `(value >> 3) * 4`, discarding bits 2..0 -- and bits 1 and 0 are the ONLY
// status signalling the part has. The .ino does the raw Wire read (I2C needs
// Arduino); this header holds the interpretation, so the host tests exercise
// the same judgement the firmware runs.
//
// Register layout (TI SBOS448G Figure 24; docs/ina219-register-reference.md):
//
//   15..3  BD12..BD0  bus voltage, LSB = 4 mV
//   2      --         unused
//   1      CNVR       conversion ready
//   0      OVF        math overflow
//
// ---------------------------------------------------------------------------
// Why CNVR gates the read (the frozen-register lesson, 2026-07-27/28)
// ---------------------------------------------------------------------------
//
// A powered-down INA219 still ACKs and still serves its registers -- it just
// never converts, so every read returns the last pre-powerdown contents. That
// is how the panel telemetry froze for a whole night: plausible values,
// dutifully served, measuring nothing. A blind post-wake delay cannot tell
// "converted" from "served stale contents"; CNVR can, and its clear conditions
// fit this firmware's access pattern exactly (SBOS448G 8.6.3.2):
//
//   * powerSave(false) writes MODE=111 -> CLEARS CNVR (a mode write, and not
//     to power-down/disable, which are the two excepted values)
//   * powerSave(true) writes MODE=000 -> does NOT clear it (excepted)
//   * a Power-register (03h) read clears it -- this firmware never reads 03h,
//     so polling 02h is side-effect-free
//
// So wake -> poll 02h until CNVR -> read is unambiguous: CNVR set means a
// conversion completed AFTER the wake.
//
// ---------------------------------------------------------------------------
// Why OVF is worth a fault bit
// ---------------------------------------------------------------------------
//
// OVF means the Current/Power calculations are out of range -- "data may be
// meaningless". In THIS configuration it should be unreachable: the shunt
// clips at +-400 mA (PG=/1) before Current can overflow, and Power full scale
// is ~65 W against a <=6.4 W ceiling. So OVF firing means something structural
// -- most plausibly a corrupted Calibration register, a live risk because
// getCurrent_raw() rewrites 05h on EVERY read. Bus voltage stays valid under
// OVF (it is a direct ADC result, not a calculation); current does not.
//
#include <stdint.h>

#define INA219_REG_BUS_ADDR 0x02

#define INA219_BUS_CNVR_MASK 0x0002u
#define INA219_BUS_OVF_MASK  0x0001u

// Poll budget for CNVR after the powerSave(false) wake. The configured mode
// (12-bit, single sample, shunt+bus) converts in <= 586 us x 2 = 1.17 ms worst
// case, so 10 ms is an 8.5x margin. If BADC/SADC are ever moved to averaging,
// this must exceed the new conversion time (128-sample averaging = 68.1 ms) --
// and the failure mode of forgetting is a LOUD fault every wake, not silent
// stale data, which is the whole point of gating on CNVR instead of a delay.
#define INA219_CNVR_TIMEOUT_MS 10u

inline bool ina219BusConversionReady(uint16_t raw) {
  return (raw & INA219_BUS_CNVR_MASK) != 0;
}

inline bool ina219BusOverflow(uint16_t raw) {
  return (raw & INA219_BUS_OVF_MASK) != 0;
}

// Bus voltage in millivolts: 13-bit field in bits 15..3, LSB 4 mV.
inline uint16_t ina219BusMillivolts(uint16_t raw) {
  return (uint16_t)((raw >> 3) * 4u);
}

// Is a live read trustworthy enough to feed the sun EWMA and the harvest
// accumulator? ALL of:
//   * the I2C transactions completed (bus + current reads each ACKed)
//   * a conversion finished after the wake (CNVR) -- the frozen-register guard
//   * the value is physically plausible (< 20 V; a wedged-but-ACKing bus can
//     serve garbage). Note there is deliberately NO lower bound: a dark panel
//     legitimately reads ~0 mV all night, and that IS the sun signal working.
//
// OVF is deliberately NOT part of this verdict: bus voltage remains valid
// under overflow, so the EWMA keeps its input; the caller zeroes the CURRENT
// (which overflow does invalidate) and raises the OVF fault separately.
inline bool ina219LiveReadOk(bool busReadOk, bool conversionReady,
                             bool currentReadOk, uint16_t busMv) {
  return busReadOk && conversionReady && currentReadOk && busMv < 20000u;
}
