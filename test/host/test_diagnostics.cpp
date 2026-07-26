// Host tests for diagnostics.h -- the error/health uplink logic.
//
// Covers the three pieces with judgement in them: which faults a given set of
// inputs raises, how the frame is serialised, and the send/rate-limit decision
// (Option A: boot frame + rate-limited fault, spam-proof with or without a clock).
//
// Build and run:  test/host/run_tests.sh

#include "../../diagnostics.h"
#include <cstdio>

static int failures = 0;

static void check(bool ok, const char *what) {
  if (ok) { std::printf("  ok    %s\n", what); }
  else    { std::printf("  FAIL  %s\n", what); failures++; }
}

// A healthy solar unit: one sensor, good reads, valid clock, full-ish pack.
static DiagInputs healthy() {
  DiagInputs in{};
  in.isSolar = true;
  in.isDev = true;
  in.resetCause = 0x40;      // SYST (system reset request)
  in.bootCounter = 1;
  in.ds18Count = 1;
  in.ds18ReadValid = true;
  in.coldBoot = false;
  in.persistCorrupt = false;
  in.ina219Present = true;
  in.ina219ReadOk = true;
  in.probeConfig = 0x399F;
  in.clockValid = true;
  in.lastTxTimeout = false;
  in.vbatMv = 4100;
  return in;
}

int main() {
  std::printf("\ndiagnostics -- error/health uplink\n");

  // -------------------------------------------------------------------------
  // 1. Faults: each condition raises exactly its bit, health raises nothing.
  // -------------------------------------------------------------------------
  { DiagInputs in = healthy();
    check(diagComputeFaults(&in) == 0, "healthy inputs raise no faults"); }

  { DiagInputs in = healthy(); in.ds18Count = 0;
    check(diagComputeFaults(&in) == DIAG_FAULT_DS18B20_NOT_FOUND, "count 0 -> DS18B20 not found"); }
  { DiagInputs in = healthy(); in.ds18Count = 2;
    check(diagComputeFaults(&in) == DIAG_FAULT_DS18B20_BUS_AMBIG, "count 2 -> bus ambiguous"); }
  { DiagInputs in = healthy(); in.ds18ReadValid = false;
    check(diagComputeFaults(&in) == DIAG_FAULT_DS18B20_READ_FAIL, "1 device, bad read -> read fail"); }
  { DiagInputs in = healthy(); in.ina219ReadOk = false;
    check(diagComputeFaults(&in) == DIAG_FAULT_INA219_READ_FAIL, "solar, INA219 bad read -> INA219 read fail"); }

  // A PRIMARY board with no INA219 is NOT a fault -- one binary, no per-unit config.
  { DiagInputs in = healthy(); in.isSolar = false; in.ina219Present = false; in.ina219ReadOk = false;
    check(diagComputeFaults(&in) == 0, "primary with no INA219 raises no fault (by design)"); }

  { DiagInputs in = healthy(); in.persistCorrupt = true;
    check(diagComputeFaults(&in) == DIAG_FAULT_PERSIST_CORRUPT, "persist corrupt -> fault"); }
  { DiagInputs in = healthy(); in.lastTxTimeout = true;
    check(diagComputeFaults(&in) == DIAG_FAULT_TX_TIMEOUT, "last TX timeout -> fault"); }
  { DiagInputs in = healthy(); in.vbatMv = DIAG_LOW_BATT_MV - 1;
    check(diagComputeFaults(&in) & DIAG_FAULT_LOW_BATTERY, "vbat below floor -> low battery"); }
  { DiagInputs in = healthy(); in.vbatMv = DIAG_LOW_BATT_MV;
    check((diagComputeFaults(&in) & DIAG_FAULT_LOW_BATTERY) == 0, "vbat exactly at floor is NOT low"); }

  // Multiple faults compose.
  { DiagInputs in = healthy(); in.ds18Count = 0; in.lastTxTimeout = true;
    check(diagComputeFaults(&in) == (DIAG_FAULT_DS18B20_NOT_FOUND | DIAG_FAULT_TX_TIMEOUT),
          "faults compose into the bitmap"); }

  // -------------------------------------------------------------------------
  // 2. Encoding: fixed length, fields land in the right bytes.
  // -------------------------------------------------------------------------
  {
    DiagInputs in = healthy();
    in.ds18Count = 0;                       // -> a fault we can see in bytes 5-6
    uint16_t faults = diagComputeFaults(&in);
    uint8_t buf[DIAG_PAYLOAD_LEN];
    uint8_t n = diagEncode(buf, &in, faults);
    check(n == DIAG_PAYLOAD_LEN, "diagEncode returns DIAG_PAYLOAD_LEN");
    check(buf[0] == DIAG_SCHEMA_VERSION, "byte 0 is the schema version");
    check((buf[1] & DIAG_INFO_SOLAR) && (buf[1] & DIAG_INFO_DEV) &&
          (buf[1] & DIAG_INFO_CLOCK_VALID) && (buf[1] & DIAG_INFO_INA219_SEEN) &&
          !(buf[1] & DIAG_INFO_COLD_BOOT),
          "byte 1 info bits reflect solar/dev/clock/ina219, cold-boot clear");
    check(buf[2] == 0x40, "byte 2 is the reset cause");
    check(buf[3] == 1, "byte 3 is the boot counter");
    check(buf[4] == 0, "byte 4 is the OneWire count");
    check(((buf[5] << 8) | buf[6]) == DIAG_FAULT_DS18B20_NOT_FOUND, "bytes 5-6 are the fault bitmap (BE)");
    check(((buf[7] << 8) | buf[8]) == 0x399F, "bytes 7-8 are the probe config (BE)");
    check(((buf[9] << 8) | buf[10]) == 4100, "bytes 9-10 are battery mV (BE)");
  }

  // -------------------------------------------------------------------------
  // 3. Send decision -- boot frame always sends.
  // -------------------------------------------------------------------------
  check(diagShouldSend(true, 0, 0, 0, 1000, true, 86400),
        "boot frame sends even with no faults");

  // No fault, not a boot frame -> silence.
  check(!diagShouldSend(false, 0, 0, 0, 1000, true, 86400),
        "no fault, not boot -> no send");

  // -------------------------------------------------------------------------
  // 4. New fault sends once; the same fault does not re-send until minResend.
  // -------------------------------------------------------------------------
  {
    uint16_t lastFaults = 0; uint32_t lastEpoch = 0;
    const uint32_t MIN = 86400;              // 1 day
    uint16_t f = DIAG_FAULT_DS18B20_NOT_FOUND;

    // t=1000: fault appears, never reported -> send.
    check(diagShouldSend(false, f, lastFaults, lastEpoch, 1000, true, MIN),
          "new fault -> sends");
    diagMarkSent(&lastFaults, &lastEpoch, f, 1000, true);
    check(lastFaults == f && lastEpoch == 1000, "mark records the fault and time");

    // t=1000+3600: same fault, well within the day -> suppressed.
    check(!diagShouldSend(false, f, lastFaults, lastEpoch, 1000 + 3600, true, MIN),
          "same fault within minResend -> suppressed");

    // t=1000+MIN: a day later -> periodic re-alert.
    check(diagShouldSend(false, f, lastFaults, lastEpoch, 1000 + MIN, true, MIN),
          "same fault after minResend -> re-alert");

    // A SECOND, distinct fault (low battery) appears within the day. It is a bit
    // we have never reported, so it reports PROMPTLY -- a new distinct fault is
    // never delayed by an unrelated one that was reported earlier.
    uint16_t f2 = f | DIAG_FAULT_LOW_BATTERY;
    check(diagShouldSend(false, f2, lastFaults, lastEpoch, 1000 + 3600, true, MIN),
          "a NEW distinct fault reports promptly despite the rate limit");
    // ...but once reported (latched), that same pair does not re-fire until minResend.
    diagMarkSent(&lastFaults, &lastEpoch, f2, 1000 + 3600, true);
    check(lastFaults == f2, "edge send latches the new bit without dropping the old");
    check(!diagShouldSend(false, f2, lastFaults, lastEpoch, 1000 + 7200, true, MIN),
          "the latched fault pair is then rate-limited");
  }

  // -------------------------------------------------------------------------
  // 5. No clock: each distinct fault is reported exactly once, no spam.
  // -------------------------------------------------------------------------
  {
    uint16_t lastFaults = 0; uint32_t lastEpoch = 0;
    uint16_t f = DIAG_FAULT_DS18B20_READ_FAIL;

    check(diagShouldSend(false, f, lastFaults, lastEpoch, 0, false, 86400),
          "no clock: first appearance sends");
    diagMarkSent(&lastFaults, &lastEpoch, f, 0, false);
    check(lastFaults == f, "no clock: mark ACCUMULATES the bit");
    check(lastEpoch == 0, "no clock: epoch stays unset");

    // Same fault flapping every cycle -> never re-sends (already accumulated).
    check(!diagShouldSend(false, f, lastFaults, lastEpoch, 0, false, 86400),
          "no clock: same fault does not re-send (spam-proof)");
    check(!diagShouldSend(false, 0, lastFaults, lastEpoch, 0, false, 86400),
          "no clock: fault clears -> no send");
    check(!diagShouldSend(false, f, lastFaults, lastEpoch, 0, false, 86400),
          "no clock: fault reappears -> still suppressed (bit already reported)");

    // A genuinely new bit still gets its one report.
    uint16_t f2 = DIAG_FAULT_LOW_BATTERY;
    check(diagShouldSend(false, f2, lastFaults, lastEpoch, 0, false, 86400),
          "no clock: a NEW distinct fault still reports once");
    diagMarkSent(&lastFaults, &lastEpoch, f2, 0, false);
    check(lastFaults == (uint16_t)(f | f2), "no clock: bits accumulate across faults");
  }

  std::printf("\n%s\n\n", failures ? "FAILED" : "all passed");
  return failures ? 1 : 0;
}
