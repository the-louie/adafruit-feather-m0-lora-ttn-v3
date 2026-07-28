#pragma once
//
// Field-observable diagnostics: a dedicated error/health uplink on its own FPort,
// so a deployed unit with no USB can still report a dead DS18B20, a flaky I2C
// bus, a corrupt .noinit restore, a TX timeout, or a dying pack.
//
// Why a SEPARATE FPort and not more status bits in the data payload:
//   * The core 9-byte payload carries NO status at all (only the solar variant's
//     appended byte 14 does), so a PRIMARY board is otherwise completely blind.
//   * A diagnostic frame can fire the moment a fault appears, instead of waiting
//     for the next batch-of-six data uplink.
//   * It carries context that does not fit the fixed data payload: reset cause,
//     the INA219 probe result, the OneWire device count.
//
// Duty cycle and battery are load-bearing constraints here, so diagnostics are
// RARE by construction (see diagShouldSend): one frame per boot, plus one when a
// new fault appears, rate-limited so a persistent fault cannot spam.
//
// Pure logic, no Arduino dependencies (only <stdint.h>), so the host tests
// exercise the same code the firmware runs. The .ino gathers the raw inputs
// (PM->RCAUSE, the I2C probe result, the OneWire count) and calls in here.
//
#include <stdint.h>

// Schema version for the diagnostic payload. Bump if the byte layout changes;
// the decoder keys on it.
#define DIAG_SCHEMA_VERSION 1
#define DIAG_PAYLOAD_LEN    11

// ---------------------------------------------------------------------------
// Fault bitmap -- payload bytes 5..6, big-endian. Each bit is an ACTIONABLE
// fault, not merely informational state (variant/clock/boot live in the info
// byte instead).
// ---------------------------------------------------------------------------
#define DIAG_FAULT_DS18B20_NOT_FOUND  0x0001u  // OneWire count == 0 (dead / unplugged)
#define DIAG_FAULT_DS18B20_BUS_AMBIG  0x0002u  // OneWire count  > 1 (cannot attribute)
#define DIAG_FAULT_DS18B20_READ_FAIL  0x0004u  // one device present, but its read failed
#define DIAG_FAULT_INA219_READ_FAIL   0x0008u  // solar: INA219 seen at boot, live read bad
#define DIAG_FAULT_PERSIST_CORRUPT    0x0010u  // .noinit looked ours but the CRC failed
#define DIAG_FAULT_TX_TIMEOUT         0x0020u  // an uplink failed (timeout or refused) since last report
#define DIAG_FAULT_LOW_BATTERY        0x0040u  // vbat below the hard floor

// Notably ABSENT: "INA219 missing". One binary serves every board, so a primary
// unit legitimately finds no INA219 -- that is not a fault, and flagging it would
// be noise on every primary board. A solar board that mis-probes shows up as the
// wrong FPort instead (the existing A1 backend alarm), which this frame's info
// byte + probe-config field make explicit.

// ---------------------------------------------------------------------------
// Info byte -- payload byte 1. State the backend wants for context, not alarms.
// ---------------------------------------------------------------------------
#define DIAG_INFO_SOLAR       0x01u
#define DIAG_INFO_DEV         0x02u
#define DIAG_INFO_COLD_BOOT   0x04u
#define DIAG_INFO_CLOCK_VALID 0x08u
#define DIAG_INFO_INA219_SEEN 0x10u

// Hard low-battery floor, common to BOTH packs: about the Feather's ~3.4 V
// brownout and the solar 3.45 V band. A healthy 6 V primary pack never sits this
// low and a li-ion above its cutoff does not either, so the band is empty in
// normal operation -- an unambiguous "about to die" signal for either variant.
#define DIAG_LOW_BATT_MV 3400u

// Everything the diagnostic frame reports. The .ino fills this from live state.
struct DiagInputs {
  bool     isSolar;
  bool     isDev;
  uint8_t  resetCause;     // PM->RCAUSE (low byte)
  uint8_t  bootCounter;    // persist.bootCounter
  uint8_t  ds18Count;      // OneWire device count on the sensor bus
  bool     ds18ReadValid;  // the last surface reading was a real number
  bool     coldBoot;       // persist was NOT restored this boot
  bool     persistCorrupt; // magic+version matched but the CRC did not (decayed RAM)
  bool     ina219Present;   // the boot probe found the INA219
  bool     ina219ReadOk;    // solar: a live INA219 read looked plausible
  uint16_t probeConfig;     // INA219 config register read during the probe (0 if none)
  bool     clockValid;
  bool     lastTxTimeout;   // an uplink (data or out-of-band) failed since the
                            // last successful diagnostic report -- a latch the
                            // .ino clears only after this frame transmits
  uint16_t vbatMv;
};

// Which faults are present right now. Pure function of the gathered inputs.
inline uint16_t diagComputeFaults(const DiagInputs *in) {
  uint16_t f = 0;
  if (in->ds18Count == 0) f |= DIAG_FAULT_DS18B20_NOT_FOUND;
  if (in->ds18Count > 1)  f |= DIAG_FAULT_DS18B20_BUS_AMBIG;
  if (in->ds18Count == 1 && !in->ds18ReadValid) f |= DIAG_FAULT_DS18B20_READ_FAIL;
  if (in->isSolar && in->ina219Present && !in->ina219ReadOk)
    f |= DIAG_FAULT_INA219_READ_FAIL;
  if (in->persistCorrupt) f |= DIAG_FAULT_PERSIST_CORRUPT;
  if (in->lastTxTimeout)  f |= DIAG_FAULT_TX_TIMEOUT;
  if (in->vbatMv < DIAG_LOW_BATT_MV) f |= DIAG_FAULT_LOW_BATTERY;
  return f;
}

// Serialise the diagnostic frame. Returns DIAG_PAYLOAD_LEN.
//
//   0     schema version
//   1     info bits (variant / mode / cold boot / clock / INA219 seen)
//   2     reset cause (PM->RCAUSE)
//   3     boot counter
//   4     OneWire device count
//   5-6   fault bitmap, big-endian
//   7-8   INA219 probe config register, big-endian (0x399F = healthy; 0 = none)
//   9-10  battery millivolts, big-endian (raw mV; decoder divides by 1000)
inline uint8_t diagEncode(uint8_t *buf, const DiagInputs *in, uint16_t faults) {
  buf[0] = DIAG_SCHEMA_VERSION;
  uint8_t info = 0;
  if (in->isSolar)       info |= DIAG_INFO_SOLAR;
  if (in->isDev)         info |= DIAG_INFO_DEV;
  if (in->coldBoot)      info |= DIAG_INFO_COLD_BOOT;
  if (in->clockValid)    info |= DIAG_INFO_CLOCK_VALID;
  if (in->ina219Present) info |= DIAG_INFO_INA219_SEEN;
  buf[1] = info;
  buf[2] = in->resetCause;
  buf[3] = in->bootCounter;
  buf[4] = in->ds18Count;
  buf[5] = (uint8_t)(faults >> 8);
  buf[6] = (uint8_t)(faults & 0xFF);
  buf[7] = (uint8_t)(in->probeConfig >> 8);
  buf[8] = (uint8_t)(in->probeConfig & 0xFF);
  buf[9]  = (uint8_t)(in->vbatMv >> 8);
  buf[10] = (uint8_t)(in->vbatMv & 0xFF);
  return DIAG_PAYLOAD_LEN;
}

// Should a diagnostic frame transmit this cycle? (Option A: boot + rate-limited
// fault.) State survives resets in persist: lastSentFaults / lastSentEpoch.
//
//   * bootFrame -> always (one frame per boot, sent after the first read).
//   * a fault bit we have NOT already reported (edge) -> report PROMPTLY, so a
//     new distinct fault (say low battery) is never delayed by an unrelated fault
//     that was reported earlier. A reported bit stays latched (diagMarkSent
//     accumulates), so it cannot re-fire until it is re-baselined -- no spam.
//   * the same faults persisting -> re-alert once minResend seconds pass. This
//     also re-baselines the latch, so a fault that cleared long ago and returns
//     is caught here (its epoch is stale) rather than being silently latched off.
//
// Clock dependency is deliberate: the periodic re-alert needs a wall clock, which
// may be invalid early in a unit's life. Until the clock lands, only edges fire
// (each distinct fault exactly once), which is inherently spam-proof.
inline bool diagShouldSend(bool bootFrame, uint16_t faults,
                           uint16_t lastSentFaults, uint32_t lastSentEpoch,
                           uint32_t nowEpoch, bool clockValid,
                           uint32_t minResendSeconds) {
  if (bootFrame) return true;
  if (faults == 0) return false;

  uint16_t unreported = (uint16_t)(faults & ~lastSentFaults);
  if (unreported != 0) return true;   // a distinct new fault -> report promptly

  // All present faults already reported: periodic re-alert only, clock required.
  return clockValid && lastSentEpoch != 0 &&
         nowEpoch >= lastSentEpoch + minResendSeconds;
}

// Record that a diagnostic frame was actually transmitted. Distinguishes an edge
// send (a new bit appeared -> accumulate the latch, keep the clock running) from
// a periodic re-alert (no new bit -> re-baseline to the current faults so cleared
// ones drop). Reads *lastSentFaults BEFORE updating it to tell the two apart.
inline void diagMarkSent(uint16_t *lastSentFaults, uint32_t *lastSentEpoch,
                         uint16_t faults, uint32_t nowEpoch, bool clockValid) {
  bool edge = (uint16_t)(faults & ~*lastSentFaults) != 0;
  if (edge) {
    *lastSentFaults = (uint16_t)(*lastSentFaults | faults);  // latch reported bits
    if (clockValid && *lastSentEpoch == 0) *lastSentEpoch = nowEpoch;  // start the re-alert clock
  } else {
    *lastSentFaults = faults;              // re-baseline: drop faults that have cleared
    if (clockValid) *lastSentEpoch = nowEpoch;
  }
}

// ---------------------------------------------------------------------------
// Verbose DEV diagnostics frame (own FPort, DEV-only).
//
// The fault frame above only speaks up on faults. During bench/DEV bring-up we
// also want to see the FULL live state on a steady cadence and confirm
// everything looks OK -- battery, panel V/I, sun EWMA, harvest, season/band,
// interval, sensor, INA219 config -- not just "no faults". This is that snapshot.
//
// DEV-only by construction: the .ino gates the whole path on runMode==DEV, so it
// never spends airtime/battery in the field. Battery is deliberately not a
// concern in DEV, so it goes out on a fixed cadence with no rate/fault gating.
// ---------------------------------------------------------------------------
#define DIAG_VERBOSE_SCHEMA 1
#define DIAG_VERBOSE_LEN    22

// Extra info bits used only by the verbose frame (bytes 0-4 of the info byte are
// the shared DIAG_INFO_* above).
#define DIAG_INFO_BONUS_ACTIVE 0x20
#define DIAG_INFO_BUS_AMBIG    0x40

// Sentinel for an unavailable surface temperature (NaN / disconnected sensor).
#define VERBOSE_TEMP_INVALID ((int16_t)0x7FFF)

struct VerboseSnapshot {
  bool     isSolar;
  bool     isDev;
  bool     coldBoot;
  bool     clockValid;
  bool     ina219Present;
  bool     bonusActive;
  bool     busAmbiguous;
  uint8_t  resetCause;
  uint8_t  bootCounter;
  uint8_t  intervalIndex;         // 0..10
  uint8_t  seasonState;           // season.h SeasonState: 0 WINTER, 1 MID, 2 SUMMER.
                                  // (This comment previously said the opposite --
                                  // the exact inversion that shipped as the
                                  // decoder bug fixed in 8dc181f. season.h is
                                  // the authority; never restate the order from
                                  // memory.)
  uint8_t  voltageBand;           // 0..3
  uint16_t batteryMv;
  uint16_t panelBusMv;
  uint16_t panelCurrentTenthMa;   // 0.1 mA/LSB
  uint8_t  sunEwma255;            // 0..255 = 0.0..1.0
  uint16_t harvestMah;
  uint16_t ina219Config;
  uint8_t  ds18Count;
  int16_t  surfaceTempCenti;      // centi-degC; VERBOSE_TEMP_INVALID if unavailable
  uint16_t faults;                // same bitmap as the fault frame (0 = all clear)
};

// Serialise the verbose frame. Returns DIAG_VERBOSE_LEN. Byte map documented in
// TODO.md item 16 and mirrored by the decoder's decodeVerbose().
inline uint8_t diagEncodeVerbose(uint8_t *buf, const VerboseSnapshot *v) {
  buf[0] = DIAG_VERBOSE_SCHEMA;
  uint8_t info = 0;
  if (v->isSolar)       info |= DIAG_INFO_SOLAR;
  if (v->isDev)         info |= DIAG_INFO_DEV;
  if (v->coldBoot)      info |= DIAG_INFO_COLD_BOOT;
  if (v->clockValid)    info |= DIAG_INFO_CLOCK_VALID;
  if (v->ina219Present) info |= DIAG_INFO_INA219_SEEN;
  if (v->bonusActive)   info |= DIAG_INFO_BONUS_ACTIVE;
  if (v->busAmbiguous)  info |= DIAG_INFO_BUS_AMBIG;
  buf[1] = info;
  buf[2] = v->resetCause;
  buf[3] = v->bootCounter;
  buf[4] = v->intervalIndex;
  buf[5] = (uint8_t)((v->seasonState & 0x03) | ((v->voltageBand & 0x03) << 2));
  buf[6]  = (uint8_t)(v->batteryMv >> 8);           buf[7]  = (uint8_t)(v->batteryMv & 0xFF);
  buf[8]  = (uint8_t)(v->panelBusMv >> 8);          buf[9]  = (uint8_t)(v->panelBusMv & 0xFF);
  buf[10] = (uint8_t)(v->panelCurrentTenthMa >> 8); buf[11] = (uint8_t)(v->panelCurrentTenthMa & 0xFF);
  buf[12] = v->sunEwma255;
  buf[13] = (uint8_t)(v->harvestMah >> 8);          buf[14] = (uint8_t)(v->harvestMah & 0xFF);
  buf[15] = (uint8_t)(v->ina219Config >> 8);        buf[16] = (uint8_t)(v->ina219Config & 0xFF);
  buf[17] = v->ds18Count;
  uint16_t t = (uint16_t)v->surfaceTempCenti;
  buf[18] = (uint8_t)(t >> 8);                       buf[19] = (uint8_t)(t & 0xFF);
  buf[20] = (uint8_t)(v->faults >> 8);              buf[21] = (uint8_t)(v->faults & 0xFF);
  return DIAG_VERBOSE_LEN;
}

// Should the verbose frame transmit this cycle? DEV-only: once on the first
// operational cycle after boot (sentOnce==false), then every intervalMs. The
// subtraction is unsigned so it is correct across a millis() wrap.
inline bool verboseShouldSend(bool isDev, bool sentOnce, uint32_t nowMs,
                              uint32_t lastMs, uint32_t intervalMs) {
  if (!isDev) return false;
  if (!sentOnce) return true;
  return (uint32_t)(nowMs - lastMs) >= intervalMs;
}
