// Host tests for persist.h -- the .noinit validity guard.
//
// The case that matters: a brief power interruption decays RAM PARTIALLY, so the
// magic word survives but the body is corrupt. Magic + version alone would
// restore from garbage. The CRC must catch it. That is the whole reason the CRC
// exists, and S06-06 tests the physical version on hardware.
//
// Build and run:  test/host/run_tests.sh

#include "../../persist.h"
#include <cstdio>
#include <cstring>
#include <cstddef>

static int failures = 0;

static void check(bool ok, const char *what) {
  if (ok) { std::printf("  ok    %s\n", what); }
  else    { std::printf("  FAIL  %s\n", what); failures++; }
}

int main() {
  std::printf("\npersist -- .noinit validity guard\n");

  // -------------------------------------------------------------------------
  // 1. A freshly initialised struct is valid and zeroed.
  // -------------------------------------------------------------------------
  {
    PersistState s;
    persistInit(&s);
    check(persistValid(&s), "freshly initialised state is valid");
    check(s.seasonState == 0 && s.uplinkCounter == 0 && s.harvestMilliAmpHours == 0,
          "init zeroes the body");
  }

  // -------------------------------------------------------------------------
  // 2. Sealed state survives a "reset": copy the bytes, reinterpret, validate.
  //    This is what .noinit does -- the memory is the same, the program is new.
  // -------------------------------------------------------------------------
  {
    PersistState before;
    persistInit(&before);
    before.seasonState = 2;
    before.uplinkCounter = 7;
    before.surfaceTempC = 16.8f;
    before.rtcEpoch = 1789000000u;
    persistSeal(&before);

    uint8_t sram[sizeof(PersistState)];
    std::memcpy(sram, &before, sizeof(before));   // "power stays on across reset"

    PersistState *after = (PersistState *)sram;
    check(persistValid(after), "sealed state validates after a byte-copy 'reset'");
    check(after->seasonState == 2 && after->uplinkCounter == 7 &&
          after->rtcEpoch == 1789000000u,
          "restored values are intact");
  }

  // -------------------------------------------------------------------------
  // 3. Uninitialised SRAM (true cold boot) must NOT validate.
  //    Fill with a few plausible garbage patterns.
  // -------------------------------------------------------------------------
  {
    for (int pattern = 0; pattern < 3; pattern++) {
      uint8_t sram[sizeof(PersistState)];
      std::memset(sram, pattern == 0 ? 0x00 : (pattern == 1 ? 0xFF : 0xA5), sizeof(sram));
      PersistState *s = (PersistState *)sram;
      check(!persistValid(s), pattern == 0 ? "cold boot: all-0x00 SRAM is invalid"
                            : pattern == 1 ? "cold boot: all-0xFF SRAM is invalid"
                                           : "cold boot: 0xA5 SRAM is invalid");
    }
  }

  // -------------------------------------------------------------------------
  // 4. THE CASE THAT MATTERS. Magic and version survive; one body byte flips.
  //    Magic + version would say "valid". The CRC must say "no".
  // -------------------------------------------------------------------------
  {
    PersistState s;
    persistInit(&s);
    s.uplinkCounter = 5;
    persistSeal(&s);
    check(persistValid(&s), "baseline: sealed and valid");

    // A single bit of the body decays.
    s.surfaceTempC = 16.8f;          // change a body byte WITHOUT resealing
    check(s.magic == PERSIST_MAGIC,  "partial decay: magic still intact");
    check(s.version == PERSIST_VERSION, "partial decay: version still intact");
    check(!persistValid(&s),
          "partial decay: CRC REJECTS it -- magic+version alone would have restored garbage");
  }
  {
    // Corrupt exactly one byte deep in the body, magic and version untouched.
    PersistState s;
    persistInit(&s);
    s.harvestMilliAmpHours = 1234;
    persistSeal(&s);
    uint8_t *raw = (uint8_t *)&s;
    raw[sizeof(PersistState) - 1] ^= 0x01;   // flip the lowest bit of the last field
    check(!persistValid(&s), "single-bit flip in the last body byte is rejected");
  }

  // -------------------------------------------------------------------------
  // 5. Version mismatch -> invalid. A firmware upgrade that changed the layout
  //    must not resume from the old one.
  // -------------------------------------------------------------------------
  {
    PersistState s;
    persistInit(&s);
    s.version = PERSIST_VERSION + 1;   // "written by a newer/older firmware"
    // CRC is stale now anyway, but even if it matched, version alone rejects it.
    check(!persistValid(&s), "version mismatch is rejected");

    // And prove version is checked INDEPENDENTLY of the CRC: reseal at the wrong
    // version so the CRC is self-consistent, and confirm it still fails.
    s.crc = persistComputeCrc(&s);
    check(s.crc == persistComputeCrc(&s), "  (crc now self-consistent at wrong version)");
    check(!persistValid(&s), "version mismatch rejected even with a valid CRC");
  }

  // -------------------------------------------------------------------------
  // 6. The CRC actually covers the WHOLE body, not just the front of it.
  //    Flip each body byte in turn; every one must break validity.
  // -------------------------------------------------------------------------
  {
    bool everyByteCovered = true;
    for (size_t i = PERSIST_HEADER_BYTES; i < sizeof(PersistState); i++) {
      PersistState s;
      persistInit(&s);
      persistSeal(&s);
      uint8_t *raw = (uint8_t *)&s;
      raw[i] ^= 0xFF;
      if (persistValid(&s)) everyByteCovered = false;
    }
    check(everyByteCovered, "every body byte is covered by the CRC");
  }

  // -------------------------------------------------------------------------
  // 7. Header size assumption. If the struct's header layout ever changes,
  //    PERSIST_HEADER_BYTES must change with it -- catch it here, not in the
  //    field.
  // -------------------------------------------------------------------------
  {
    // magic(4) + version(2) + crc(2) = 8, assuming no padding before the body.
    // The body starts with a uint8_t, so the compiler will not pad after crc.
    size_t headerToFirstBody = offsetof(PersistState, seasonState);
    check(headerToFirstBody == PERSIST_HEADER_BYTES,
          "PERSIST_HEADER_BYTES matches the real offset to the body");
  }

  std::printf("\n%s\n\n", failures ? "FAILED" : "all passed");
  return failures ? 1 : 0;
}
