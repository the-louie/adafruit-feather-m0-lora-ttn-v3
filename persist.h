#pragma once
//
// State that must survive NVIC_SystemReset() -- the reset the PROD join-failure
// path performs after a 15-minute sleep.
//
// It lives in a .noinit struct, NOT in flash. A soft reset does not physically
// clear SRAM; only the C runtime zeroes .bss on startup. So a variable placed in
// the .noinit section keeps its value across a reset at ZERO flash cost -- which
// is the whole reason this honours "Eradicate FlashStorage Entirely" (master-
// plan) rather than bending it. Flash writes cost power, wear the part, and
// disturb the SPI bus the radio shares.
//
// Pure logic + a validity check, no Arduino dependencies, so the host tests
// exercise the same code the firmware runs.
//
// ---------------------------------------------------------------------------
// Why magic + version is NOT enough, and the CRC is not optional
// ---------------------------------------------------------------------------
//
// Magic catches uninitialised SRAM on a true cold boot. Version catches a
// firmware upgrade that changed the layout. Neither catches CORRUPTION -- and
// this design leans on a physical claim: that SRAM survives a soft reset.
//
// A BRIEF power interruption decays RAM PARTIALLY: long enough to corrupt the
// struct body, short enough to leave a 32-bit magic word standing. The result
// is a confident restore from garbage -- exactly the failure the version guard
// exists to prevent, arriving through a different door. The CRC over the body
// closes it. S06-06 tests this on hardware; without the CRC there is no plan for
// when that test fails.
//
#include <stdint.h>
#include <string.h>

#define PERSIST_MAGIC   0x54544E31u   // "TTN1" -- change if the MEANING changes
#define PERSIST_VERSION 2             // bump on ANY field change to the body
                                      //   v2: added diag rate-limit fields

struct PersistState {
  uint32_t magic;
  uint16_t version;
  uint16_t crc;        // CRC-16 over everything after this field

  // ---- body: everything below is covered by the CRC ----
  uint8_t  seasonState;          // SeasonState enum
  uint8_t  voltageState;         // latched voltage-band offset (0..3), S02-19
  uint8_t  currentIntervalIndex; // 0..10
  uint8_t  uplinkCounter;        // 4-bit, on the wire
  uint8_t  bootCounter;          // 3-bit in the status byte; wraps at 8
  uint8_t  clockValid;           // 0/1 -- has DeviceTimeReq landed?
  uint16_t harvestMilliAmpHours; // solar; wraps, backend unwraps
  float    surfaceTempC;         // season driver, last good reading
  float    sunEwma;              // solar sun-presence average
  uint32_t rtcEpoch;             // stashed before NVIC_SystemReset (S03-06)
  uint16_t diagLastSentFaults;   // diagnostics.h rate-limit latch: faults last reported
  uint32_t diagLastSentEpoch;    // ... and when (0 = never); survives soft resets
};

// CRC-16/CCITT-FALSE. Small, no table, adequate for catching decayed RAM -- this
// is an integrity check, not a security check.
inline uint16_t persistCrc16(const uint8_t *data, uint32_t len) {
  uint16_t crc = 0xFFFF;
  for (uint32_t i = 0; i < len; i++) {
    crc ^= (uint16_t)data[i] << 8;
    for (uint8_t b = 0; b < 8; b++) {
      crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
    }
  }
  return crc;
}

// The body starts right after the crc field. offsetof would work, but this is
// explicit and cannot be fooled by padding: magic(4) + version(2) + crc(2) = 8.
#define PERSIST_HEADER_BYTES 8

inline uint16_t persistComputeCrc(const PersistState *s) {
  const uint8_t *base = (const uint8_t *)s;
  return persistCrc16(base + PERSIST_HEADER_BYTES,
                      (uint32_t)sizeof(PersistState) - PERSIST_HEADER_BYTES);
}

// True only if this looks like state WE wrote, at THIS layout version, intact.
// Any failure -> caller must treat it as a cold boot.
inline bool persistValid(const PersistState *s) {
  if (s->magic != PERSIST_MAGIC) return false;
  if (s->version != PERSIST_VERSION) return false;
  return s->crc == persistComputeCrc(s);
}

// Distinguish decayed-RAM corruption from a true cold boot: our magic and
// version survived but the CRC no longer matches. That is exactly the partial-
// decay case the CRC exists to catch (see the header comment). Surfaced so the
// diagnostics frame can report it -- a true cold boot (wrong magic) is normal on
// a first power-up and is NOT a fault. See diagnostics.h DIAG_FAULT_PERSIST_CORRUPT.
inline bool persistDecayedButFramed(const PersistState *s) {
  return s->magic == PERSIST_MAGIC &&
         s->version == PERSIST_VERSION &&
         s->crc != persistComputeCrc(s);
}

// Call after mutating the body, before the reset that must preserve it.
inline void persistSeal(PersistState *s) {
  s->magic = PERSIST_MAGIC;
  s->version = PERSIST_VERSION;
  s->crc = persistComputeCrc(s);
}

// Zero the body and seal. Used on a cold boot / version mismatch.
inline void persistInit(PersistState *s) {
  memset(s, 0, sizeof(*s));
  persistSeal(s);
}
