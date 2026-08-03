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
// RARE by construction (see diagShouldSend): one frame per boot, one when a new
// fault appears, a persistent fault re-alerted at most daily, and exactly one
// "all clear" frame (fault_bits 0) when a reported fault set empties -- so the
// backend can tell "cleared" from "still broken and rate-limited".
//
// Pure logic, no Arduino dependencies (only <stdint.h>), so the host tests
// exercise the same code the firmware runs. The .ino gathers the raw inputs
// (PM->RCAUSE, the I2C probe result, the OneWire count) and calls in here.
//
#include <stdint.h>

// Schema version for the diagnostic payload. Bump if the byte layout changes;
// the decoder keys on it. Schema 2 appends the DS18B20 status code, the
// consecutive-failure streak and the sensor ROM id (bytes 11-15); schema-1
// captures keep decoding.
#define DIAG_SCHEMA_VERSION 2
#define DIAG_PAYLOAD_LEN    16
#define DIAG_PAYLOAD_V1_LEN 11

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
#define DIAG_FAULT_INA219_OVF         0x0080u  // solar: math-overflow flag set -- current/power
                                               // out of range. Unreachable in our configuration
                                               // (shunt clips at +-400 mA first), so it firing
                                               // means something structural: most plausibly a
                                               // corrupted Calibration register, which
                                               // getCurrent_raw() rewrites on EVERY read. See
                                               // docs/ina219-register-reference.md section 3.
#define DIAG_FAULT_TEMP_IMPLAUSIBLE   0x0100u  // reading is VALID but moved faster than
                                               // water physically can (sensor_plausibility.h)
                                               // -- the sensor is likely out of the water

// NOT a wire bit: a marker kept in the HIGH bit of the persisted
// diagLastSentFaults latch. Set when the "all clear" frame (fault_bits 0) for
// the current fault episode has been transmitted, so the episode produces
// exactly one clear frame. The fault bits themselves stay latched underneath,
// which is what keeps a flapping fault on the once-per-day re-alert path
// instead of being "new" (and prompt) after every clear. Cleared whenever a
// frame carrying nonzero faults is sent. faults itself can never carry this
// bit, so no wire mask is needed.
#define DIAG_CLEAR_SENT               0x8000u

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

// DS18B20 status, one code instead of a boolean (TODO 28): three physically
// different faults used to collapse into one bit, and the 85 degC case was only
// inferable by cross-reading the data payload against the fault frame.
enum Ds18Status : uint8_t {
  DS18_OK            = 0,
  DS18_NOT_FOUND     = 1,  // nothing answered the OneWire bus
  DS18_CRC_FAIL      = 2,  // enumerated, but the read came back -127: the
                           // CRC/no-response class (DallasTemperature does not
                           // distinguish the two; both mean "bus present but
                           // unreliable" and the repair is bus integrity)
  DS18_STUCK_85      = 3,  // exactly the 85.00 power-on scratchpad default:
                           // conversion never ran. A real 85.00 reading is
                           // indistinguishable -- accepted, since water at 85
                           // is a bigger problem than a false fault
  DS18_OUT_OF_RANGE  = 4,  // a number, but outside the -50..60 sane band
  DS18_AMBIGUOUS     = 5,  // more than one device on the bus
};

// Derive the status from what the wake observed. Pure; the .ino supplies the
// enumeration count, the ambiguity flag and the final (post-retry) reading.
inline uint8_t ds18DeriveStatus(uint8_t count, bool busAmbiguous, float tempC) {
  if (count == 0) return DS18_NOT_FOUND;
  if (count > 1 || busAmbiguous) return DS18_AMBIGUOUS;
  if (tempC != tempC || tempC <= -100.0f) return DS18_CRC_FAIL;
  if (tempC == 85.0f) return DS18_STUCK_85;   // exact: the power-on default is
                                              // representable exactly in float
  if (tempC < -50.0f || tempC > 60.0f) return DS18_OUT_OF_RANGE;
  return DS18_OK;
}

// Everything the diagnostic frame reports. The .ino fills this from live state.
struct DiagInputs {
  bool     isSolar;
  bool     isDev;
  uint8_t  resetCause;     // PM->RCAUSE (low byte)
  uint8_t  bootCounter;    // persist.bootCounter
  uint8_t  ds18Count;      // OneWire device count on the sensor bus
  uint8_t  ds18Status;     // Ds18Status: what the last wake's read actually was
  bool     tempImplausible;// valid reading, impossible-for-water step (TODO 27)
  uint8_t  sensorFailStreak; // consecutive wakes with a failed read, saturating;
                             // persisted so a fault spanning reboots is
                             // distinguishable from one that does not (TODO 29)
  uint8_t  ds18Rom[3];     // low 3 bytes of the ROM serial of the LAST sensor
                             // seen this boot (0,0,0 = never saw one): enough
                             // to notice a swapped sensor (TODO 31). Kept when
                             // the sensor disappears, so a not_found frame
                             // still names which sensor was lost
  bool     coldBoot;       // persist was NOT restored this boot
  bool     persistCorrupt; // magic+version matched but the CRC did not (decayed RAM)
  bool     ina219Present;   // the boot probe found the INA219
  bool     ina219ReadOk;    // solar: a live INA219 read looked plausible
  bool     ina219Ovf;       // solar: the OVF math-overflow flag was set on the
                            // last live read (bus reg 02h bit 0)
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
  if (in->ds18Count == 1 && in->ds18Status != DS18_OK)
    f |= DIAG_FAULT_DS18B20_READ_FAIL;   // which flavour is in the status byte
  if (in->tempImplausible) f |= DIAG_FAULT_TEMP_IMPLAUSIBLE;
  if (in->isSolar && in->ina219Present && !in->ina219ReadOk)
    f |= DIAG_FAULT_INA219_READ_FAIL;
  if (in->isSolar && in->ina219Present && in->ina219Ovf)
    f |= DIAG_FAULT_INA219_OVF;
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
//   11    DS18B20 status (Ds18Status)                             [schema 2]
//   12    consecutive-failure streak, saturating                  [schema 2]
//   13-15 sensor ROM id, low 3 bytes of the serial (0 = none)     [schema 2]
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
  buf[11] = in->ds18Status;
  buf[12] = in->sensorFailStreak;
  buf[13] = in->ds18Rom[0];
  buf[14] = in->ds18Rom[1];
  buf[15] = in->ds18Rom[2];
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
  if (faults == 0) {
    // All clear. If the backend was told about faults and has NOT yet been
    // told they cleared, send exactly one clear frame (fault_bits 0). Without
    // it the last fault report stands forever -- the backend cannot tell
    // "cleared" from "still broken and rate-limited". Episodic and clockless:
    // the marker bit limits it to one frame per fault episode.
    return (uint16_t)(lastSentFaults & (uint16_t)~DIAG_CLEAR_SENT) != 0 &&
           (lastSentFaults & DIAG_CLEAR_SENT) == 0;
  }

  uint16_t unreported = (uint16_t)(faults & ~lastSentFaults);
  if (unreported != 0) return true;   // a distinct new fault -> report promptly

  // All present faults already reported: periodic re-alert only, clock required.
  return clockValid && lastSentEpoch != 0 &&
         nowEpoch >= lastSentEpoch + minResendSeconds;
}

// Record that a diagnostic frame was actually transmitted. Three cases:
// an all-clear (faults 0 -> keep the latched bits, set the clear marker so
// this episode never repeats the clear frame), an edge send (a new bit
// appeared -> accumulate the latch, drop the marker, keep the clock running),
// or a periodic re-alert (no new bit -> re-baseline to the current faults so
// cleared ones drop; the marker drops with them). The latched bits surviving
// the all-clear is deliberate: a fault that returns after its clear frame is
// then a KNOWN fault on the daily path, not a "new" one -- which is what
// bounds a flapping fault to two frames per day instead of two per flap.
// Reads *lastSentFaults BEFORE updating it to tell the cases apart.
inline void diagMarkSent(uint16_t *lastSentFaults, uint32_t *lastSentEpoch,
                         uint16_t faults, uint32_t nowEpoch, bool clockValid) {
  if (faults == 0) {
    *lastSentFaults = (uint16_t)(*lastSentFaults | DIAG_CLEAR_SENT);
    if (clockValid) *lastSentEpoch = nowEpoch;
    return;
  }
  bool edge = (uint16_t)(faults & ~*lastSentFaults) != 0;
  if (edge) {
    *lastSentFaults = (uint16_t)((*lastSentFaults & (uint16_t)~DIAG_CLEAR_SENT) | faults);
    if (clockValid && *lastSentEpoch == 0) *lastSentEpoch = nowEpoch;  // start the re-alert clock
  } else {
    *lastSentFaults = faults;              // re-baseline: drop cleared faults AND the marker
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
// Schema 2 appends uptime, wake-cycle count, buffer state and the panel
// min/mean/max profile after schema 1's 22 bytes. Schema 3 appends the 3-byte
// firmware git hash (bytes 34-36), injected at compile time by
// scripts/build.sh -- 0x000000 means an unofficial build (compiled without the
// script, hash unknown). The decoder keys on byte 0 and keeps decoding every
// older schema.
#define DIAG_VERBOSE_SCHEMA 3
#define DIAG_VERBOSE_LEN    37
#define DIAG_VERBOSE_V2_LEN 34
#define DIAG_VERBOSE_V1_LEN 22

// Extra info bits used only by the verbose frame (bytes 0-4 of the info byte are
// the shared DIAG_INFO_* above).
#define DIAG_INFO_BONUS_ACTIVE 0x20
#define DIAG_INFO_BUS_AMBIG    0x40
// Schema 2: the panel min/mean/max fields carry data (at least one sample was
// accumulated since the last verbose frame). Clear on a PRIMARY board and on
// the frame sent immediately at boot; the decoder emits nulls.
#define DIAG_INFO_PANEL_STATS  0x80

// ---------------------------------------------------------------------------
// Panel-profile accumulator (schema 2). DEV samples the panel every minute or
// two while it busy-waits through the "sleep" window; one INA219 point per
// hour was shown to be a fragile basis for the harvest figure (a single
// uncorroborated ~105 mA sample explained a 2 -> 129 mAh jump, 2026-07-28).
// min/mean/max over the hour is a real charging profile, and the spread
// directly measures the extrapolation error item 15 needs to bound.
// Pure logic; the .ino feeds it samples and resets it after each verbose TX.
// ---------------------------------------------------------------------------
struct PanelStats {
  uint16_t n;        // samples accumulated (0 = stats invalid)
  uint16_t vMinMv, vMaxMv;
  float    iMinMa, iMaxMa, iSumMa;
};

inline void panelStatsInit(PanelStats *s) {
  s->n = 0;
  s->vMinMv = 0; s->vMaxMv = 0;
  s->iMinMa = 0.0f; s->iMaxMa = 0.0f; s->iSumMa = 0.0f;
}

inline void panelStatsAdd(PanelStats *s, uint16_t busMv, float currentMa) {
  if (currentMa < 0) currentMa = 0;
  if (s->n == 0) {
    s->vMinMv = busMv; s->vMaxMv = busMv;
    s->iMinMa = currentMa; s->iMaxMa = currentMa; s->iSumMa = currentMa;
  } else {
    if (busMv < s->vMinMv) s->vMinMv = busMv;
    if (busMv > s->vMaxMv) s->vMaxMv = busMv;
    if (currentMa < s->iMinMa) s->iMinMa = currentMa;
    if (currentMa > s->iMaxMa) s->iMaxMa = currentMa;
    s->iSumMa += currentMa;
  }
  if (s->n < 65535) s->n++;
}

inline float panelStatsMeanMa(const PanelStats *s) {
  return s->n ? s->iSumMa / (float)s->n : 0.0f;
}

// Encoders shared with the data payload's conventions: current 0.5 mA/LSB,
// bus 30 mV/LSB, both clamped.
inline uint8_t panelStatsEncodeMa(float ma) {
  if (ma < 0) ma = 0;
  uint16_t code = (uint16_t)(ma / 0.5f + 0.5f);
  return code > 255 ? 255 : (uint8_t)code;
}
inline uint8_t panelStatsEncodeMv(uint16_t mv) {
  uint16_t code = mv / 30;
  return code > 255 ? 255 : (uint8_t)code;
}

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
                                  // season.h is the sole authority on this order;
                                  // a restated copy once shipped inverted as a
                                  // decoder defect, so verify against the enum
                                  // rather than trusting any comment, including
                                  // this one.
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

  // ---- schema 2 additions (item 25) ----
  uint32_t uptimeSeconds;         // millis()/1000; DEV never sleeps so this is
                                  // real elapsed time. Wraps at 49.7 days --
                                  // acceptable for a bench frame that PROD
                                  // never sends. Doubles as the EWMA-age input
                                  // for the decoder's clarity gate (24b);
                                  // after a warm reset the restored EWMA is
                                  // OLDER than uptime, so the gate errs toward
                                  // suppressing -- conservative by design.
  uint16_t cycleCount;            // wake cycles since boot (sensor reads)
  uint8_t  ramCount;              // batch fill, 0..6 (high nibble of byte 28)
  uint8_t  uplinkCounter;         // 4-bit wire counter (low nibble of byte 28)
  const PanelStats *panelStats;   // nullptr or n==0 -> stats bytes zero,
                                  // DIAG_INFO_PANEL_STATS clear

  // ---- schema 3 ----
  uint32_t gitHash24;             // low 24 bits = first 6 hex chars of the
                                  // commit this binary was built from;
                                  // 0x000000 = unofficial build (decoder
                                  // reports null). Ties every frame to an
                                  // exact source state, so "which firmware is
                                  // this device actually running?" is answered
                                  // from the wire rather than from flash
                                  // records.
};

// Serialise the verbose frame. Returns DIAG_VERBOSE_LEN. Byte map documented in
// TODO.md items 16 (schema 1, bytes 0-21) and 25 (schema 2, bytes 22-33), and
// mirrored by the decoder's decodeVerbose():
//   22-25  uptime seconds, big-endian
//   26-27  wake-cycle count, big-endian
//   28     (ramCount & 0x0F) << 4 | (uplinkCounter & 0x0F)
//   29-31  panel current min / mean / max, 0.5 mA/LSB
//   32-33  panel bus min / max, 30 mV/LSB
//   34-36  firmware git hash, 24 bits big-endian (0 = unofficial build)
inline uint8_t diagEncodeVerbose(uint8_t *buf, const VerboseSnapshot *v) {
  bool statsValid = v->panelStats != 0 && v->panelStats->n > 0;
  buf[0] = DIAG_VERBOSE_SCHEMA;
  uint8_t info = 0;
  if (v->isSolar)       info |= DIAG_INFO_SOLAR;
  if (v->isDev)         info |= DIAG_INFO_DEV;
  if (v->coldBoot)      info |= DIAG_INFO_COLD_BOOT;
  if (v->clockValid)    info |= DIAG_INFO_CLOCK_VALID;
  if (v->ina219Present) info |= DIAG_INFO_INA219_SEEN;
  if (v->bonusActive)   info |= DIAG_INFO_BONUS_ACTIVE;
  if (v->busAmbiguous)  info |= DIAG_INFO_BUS_AMBIG;
  if (statsValid)       info |= DIAG_INFO_PANEL_STATS;
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

  // ---- schema 2 ----
  buf[22] = (uint8_t)(v->uptimeSeconds >> 24);
  buf[23] = (uint8_t)(v->uptimeSeconds >> 16);
  buf[24] = (uint8_t)(v->uptimeSeconds >> 8);
  buf[25] = (uint8_t)(v->uptimeSeconds & 0xFF);
  buf[26] = (uint8_t)(v->cycleCount >> 8);          buf[27] = (uint8_t)(v->cycleCount & 0xFF);
  buf[28] = (uint8_t)(((v->ramCount & 0x0F) << 4) | (v->uplinkCounter & 0x0F));
  if (statsValid) {
    buf[29] = panelStatsEncodeMa(v->panelStats->iMinMa);
    buf[30] = panelStatsEncodeMa(panelStatsMeanMa(v->panelStats));
    buf[31] = panelStatsEncodeMa(v->panelStats->iMaxMa);
    buf[32] = panelStatsEncodeMv(v->panelStats->vMinMv);
    buf[33] = panelStatsEncodeMv(v->panelStats->vMaxMv);
  } else {
    buf[29] = buf[30] = buf[31] = buf[32] = buf[33] = 0;
  }
  buf[34] = (uint8_t)((v->gitHash24 >> 16) & 0xFF);
  buf[35] = (uint8_t)((v->gitHash24 >> 8) & 0xFF);
  buf[36] = (uint8_t)(v->gitHash24 & 0xFF);
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

// May another verbose ATTEMPT be made yet? Distinct from verboseShouldSend,
// which asks whether a frame is DUE: the due-time only advances on a
// successful transmit, so after a failed attempt the frame stays due
// continuously. The caller sits in a busy-wait loop that evaluates every
// iteration, and one failed attempt can block for the full TX-ready budget,
// so without a spacing rule a wedged radio stack turns the whole wait window
// into consecutive blocked attempts. Unsigned subtraction, wrap-safe.
inline bool verboseRetryAllowed(bool anyAttemptMade, uint32_t nowMs,
                                uint32_t lastAttemptMs, uint32_t backoffMs) {
  if (!anyAttemptMade) return true;
  return (uint32_t)(nowMs - lastAttemptMs) >= backoffMs;
}
