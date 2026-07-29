#pragma once
//
// Which power variant is this board? Decided at boot by probing the I2C bus for
// the INA219. Found -> solar. Absent -> primary-cell.
//
// One firmware image for every board -- the same reason latitude lives in the
// decoder and keys are derived from the silicon ID. There is no per-unit
// configuration in this system, and there must not be.
//
// The I2C mechanics need Arduino (Wire), so those live in the .ino. This header
// holds only the DECISION logic and the config-register sanity check, so the
// host tests can exercise the part that actually has judgement in it: how many
// probe attempts, and what a given bus result means.
//
// ---------------------------------------------------------------------------
// The failure mode this must get right
// ---------------------------------------------------------------------------
//
// The probe conflates "has an INA219" with "is a li-ion pack". A dead sensor, a
// loose wire, or a hung bus makes a SOLAR board boot the PRIMARY policy -- whose
// 5.0/4.3/3.5 V bands all sit above a full li-ion's 4.2 V. Every reading then
// scores voltage_offset 3 and the unit pins itself at interval index 10 (7 days)
// PERMANENTLY. A component fault silently decommissions the unit.
//
// Two defences:
//   * Retry the probe (a boot is rare; one transient glitch must not decide a
//     whole session).
//   * The FPort makes a misdetect observable to the backend: a solar device on
//     FPort 10/20 failed to find its INA219. That alarm (S01-09) is the safety
//     net for when the probe is wrong anyway.
//
#include <stdint.h>

#define INA219_I2C_ADDR 0x40

// INA219 config register (0x00) power-on default, per the datasheet. A bare
// address ACK is not enough -- something else could sit at 0x40 -- so we also
// confirm the config register reads its reset value.
#define INA219_REG_CONFIG        0x00
#define INA219_CONFIG_RESET_VALUE 0x399F

// After setCalibration_16V_400mA() the config register reads 0x019F, NOT the
// power-on reset default. So on a WARM MCU reset (RST button, watchdog, the PROD
// join-failure NVIC_SystemReset) the INA219 stays powered -- its own POR
// threshold is 2 V, which the 3.3 V rail never crosses -- still holding the
// calibrated config, and this probe (which recognises only 0x399F) would misread
// a present sensor as absent, booting a solar unit into the PRIMARY policy.
// probeIna219Once() in the .ino MUST soft-reset the INA219 (write config bit 15,
// RST) before reading, so a present-but-calibrated sensor comes back as 0x399F.
// Regression on gisebo-05 2026-07-27; see
// docs/dev-notes/20260727-*_ina219-warm-reset-misdetect.md.
//
// NOTE since 007a46b (2026-07-28) the value a warm reset actually leaves behind
// is 0x0198, not 0x019F: every cycle now ends in powerSave(true), which clears
// the MODE bits. The soft-reset handles both identically; the constant below is
// kept at the awake value as the historical regression vector. The full value
// enumeration lives in docs/ina219-register-reference.md.
#define INA219_CONFIG_CALIBRATED_VALUE 0x019F

#define PROBE_ATTEMPTS   3
#define PROBE_RETRY_MS   50

enum PowerVariant : uint8_t {
  VARIANT_PRIMARY = 0,
  VARIANT_SOLAR   = 1,
};

// The Calibration register (05h). RST resets ALL registers, so after the
// probe's soft reset a real INA219 must read 0x0000 here. Checking it turns the
// 16-bit config identity match into a 32-bit one for two extra I2C bytes -- the
// part has NO manufacturer/die-ID register (the map ends at 05h), so the
// register contents are the only identification available. Weighed against the
// probe philosophy (a missed present sensor is the WORSE error): the retries
// still favour present, and a device that ACKs, serves 0x399F at 00h AND
// 0x0000 at 05h but is not an INA219 is vanishingly unlikely. TODO item 20.
#define INA219_REG_CALIBRATION 0x05
#define INA219_CAL_RESET_VALUE 0x0000

// The result of one physical probe attempt. Filled in by the .ino from Wire.
struct ProbeResult {
  bool    addressAcked;   // did 0x40 ACK its address?
  bool    configRead;     // did we get a config register read back?
  uint16_t configValue;   // ... and its value
  bool    calRead;        // did we get a calibration register read back?
  uint16_t calValue;      // ... and its value (must be 0x0000 after RST)
};

// Is a single attempt a confident "INA219 present"?
// Needs the ACK, the post-reset config value AND the post-reset calibration
// value -- an ACK alone could be some other 0x40 device, a garbage read means a
// flaky bus, and both registers together are as close to an identity as a part
// with no ID register can offer.
inline bool probeAttemptFoundIna219(const ProbeResult *r) {
  return r->addressAcked && r->configRead &&
         r->configValue == INA219_CONFIG_RESET_VALUE &&
         r->calRead && r->calValue == INA219_CAL_RESET_VALUE;
}

// Decide the variant from a run of attempts.
//
// SOLAR if ANY attempt cleanly found the INA219. Retrying favours "present":
// a real sensor that ACKs once is present, whereas noise that produces one bad
// read across three tries is not a reason to boot the wrong policy. The cost of
// missing a present INA219 (7-day interval, silent) is far worse than the cost
// of a spurious solar detection (wrong bands, but LOUD -- the backend sees a
// li-ion pack reading below 4.5 V).
inline PowerVariant probeDecide(const ProbeResult *attempts, uint8_t n) {
  for (uint8_t i = 0; i < n; i++) {
    if (probeAttemptFoundIna219(&attempts[i])) return VARIANT_SOLAR;
  }
  return VARIANT_PRIMARY;
}

// The backend-side sanity check, mirrored here so the firmware can log a warning
// too. A PRIMARY variant reading below this cannot be a healthy 6 V pack and is
// almost certainly a solar board whose INA219 was not detected. No li-ion ever
// reaches 4.5 V either, so the band between is empty and this cannot false-fire.
#define PRIMARY_IMPLAUSIBLE_VBAT 4.5f

inline bool primaryVbatImplausible(PowerVariant v, float vbat) {
  return v == VARIANT_PRIMARY && vbat < PRIMARY_IMPLAUSIBLE_VBAT;
}
