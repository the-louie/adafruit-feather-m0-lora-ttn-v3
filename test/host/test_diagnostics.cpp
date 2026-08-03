// Host tests for diagnostics.h -- the error/health uplink logic.
//
// Covers the three pieces with judgement in them: which faults a given set of
// inputs raises, how the frame is serialised, and the send/rate-limit decision
// (Option A: boot frame + rate-limited fault, spam-proof with or without a clock).
//
// Build and run:  test/host/run_tests.sh

#include "../../diagnostics.h"
#include <cstdio>
#include <cmath>

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
  in.ds18Status = DS18_OK;
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
  { DiagInputs in = healthy(); in.ds18Status = DS18_CRC_FAIL;
    check(diagComputeFaults(&in) == DIAG_FAULT_DS18B20_READ_FAIL, "1 device, bad read -> read fail"); }
  { DiagInputs in = healthy(); in.ds18Status = DS18_STUCK_85;
    check(diagComputeFaults(&in) == DIAG_FAULT_DS18B20_READ_FAIL,
          "stuck-85 raises READ_FAIL; the status byte says which flavour"); }
  { DiagInputs in = healthy(); in.tempImplausible = true;
    check(diagComputeFaults(&in) == DIAG_FAULT_TEMP_IMPLAUSIBLE,
          "implausible water step -> its own fault bit (TODO 27)"); }

  // Status derivation (TODO 28): pure function of what the wake observed.
  check(ds18DeriveStatus(0, false, NAN) == DS18_NOT_FOUND, "derive: count 0 -> NOT_FOUND");
  check(ds18DeriveStatus(2, true, 20.0f) == DS18_AMBIGUOUS, "derive: count 2 -> AMBIGUOUS");
  check(ds18DeriveStatus(1, false, -127.0f) == DS18_CRC_FAIL, "derive: -127 -> CRC/no-response");
  check(ds18DeriveStatus(1, false, NAN) == DS18_CRC_FAIL, "derive: NaN -> CRC/no-response");
  check(ds18DeriveStatus(1, false, 85.0f) == DS18_STUCK_85, "derive: exactly 85.00 -> STUCK_85");
  check(ds18DeriveStatus(1, false, 84.9f) == DS18_OUT_OF_RANGE, "derive: 84.9 -> out of range, NOT stuck");
  check(ds18DeriveStatus(1, false, 72.0f) == DS18_OUT_OF_RANGE, "derive: 72 -> out of range");
  check(ds18DeriveStatus(1, false, 21.5f) == DS18_OK, "derive: a sane reading -> OK");
  check(ds18DeriveStatus(1, false, -0.5f) == DS18_OK, "derive: near-freezing water -> OK");
  { DiagInputs in = healthy(); in.ina219ReadOk = false;
    check(diagComputeFaults(&in) == DIAG_FAULT_INA219_READ_FAIL, "solar, INA219 bad read -> INA219 read fail"); }
  { DiagInputs in = healthy(); in.ina219Ovf = true;
    check(diagComputeFaults(&in) == DIAG_FAULT_INA219_OVF, "solar, OVF set -> math-overflow fault"); }
  // OVF and read-fail are independent axes: a read can complete (ACK + CNVR)
  // and still carry the overflow flag, or fail with no flag at all.
  { DiagInputs in = healthy(); in.ina219ReadOk = false; in.ina219Ovf = true;
    check(diagComputeFaults(&in) == (DIAG_FAULT_INA219_READ_FAIL | DIAG_FAULT_INA219_OVF),
          "bad read + OVF -> both faults, neither masks the other"); }

  // A PRIMARY board with no INA219 is NOT a fault -- one binary, no per-unit config.
  { DiagInputs in = healthy(); in.isSolar = false; in.ina219Present = false; in.ina219ReadOk = false;
    check(diagComputeFaults(&in) == 0, "primary with no INA219 raises no fault (by design)"); }
  { DiagInputs in = healthy(); in.isSolar = false; in.ina219Present = false; in.ina219Ovf = true;
    check(diagComputeFaults(&in) == 0, "primary: OVF input cannot fault either (guarded on solar+present)"); }

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
    check(n == DIAG_PAYLOAD_LEN, "diagEncode returns DIAG_PAYLOAD_LEN (16, schema 2)");
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
    check(buf[0] == 2, "schema byte is 2");
  }
  { // schema-2 tail: status, streak, ROM
    DiagInputs in = healthy();
    in.ds18Status = DS18_STUCK_85;
    in.sensorFailStreak = 42;
    in.ds18Rom[0] = 0xAB; in.ds18Rom[1] = 0xCD; in.ds18Rom[2] = 0xEF;
    uint8_t buf[DIAG_PAYLOAD_LEN];
    diagEncode(buf, &in, diagComputeFaults(&in));
    check(buf[11] == DS18_STUCK_85, "byte 11 is the DS18B20 status");
    check(buf[12] == 42, "byte 12 is the failure streak");
    check(buf[13] == 0xAB && buf[14] == 0xCD && buf[15] == 0xEF,
          "bytes 13-15 are the ROM id, low 3 serial bytes");
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
    // Item 32: a reported fault clearing IS news -- exactly one all-clear frame.
    check(diagShouldSend(false, 0, lastFaults, lastEpoch, 0, false, 86400),
          "no clock: reported fault clears -> the one all-clear frame sends");
    diagMarkSent(&lastFaults, &lastEpoch, 0, 0, false);
    check(!diagShouldSend(false, 0, lastFaults, lastEpoch, 0, false, 86400),
          "no clock: all-clear already sent -> silence");
    check(!diagShouldSend(false, f, lastFaults, lastEpoch, 0, false, 86400),
          "no clock: fault reappears -> still suppressed (bit stays latched through the clear)");

    // A genuinely new bit still gets its one report.
    uint16_t f2 = DIAG_FAULT_LOW_BATTERY;
    check(diagShouldSend(false, f2, lastFaults, lastEpoch, 0, false, 86400),
          "no clock: a NEW distinct fault still reports once");
    diagMarkSent(&lastFaults, &lastEpoch, f2, 0, false);
    check(lastFaults == (uint16_t)(f | f2),
          "no clock: bits accumulate across faults, and the edge send drops the clear marker");
  }

  // -------------------------------------------------------------------------
  // 5b. Item 32: the all-clear frame -- one per episode, flap-proof.
  // -------------------------------------------------------------------------
  {
    uint16_t lastFaults = 0; uint32_t lastEpoch = 0;
    const uint32_t MIN = 86400;
    uint16_t f = DIAG_FAULT_DS18B20_NOT_FOUND;

    // Nothing ever reported -> a clear state is not news.
    check(!diagShouldSend(false, 0, lastFaults, lastEpoch, 1000, true, MIN),
          "clear with an empty latch -> no spurious all-clear");

    // Episode: fault -> alert -> clear -> exactly one all-clear.
    diagMarkSent(&lastFaults, &lastEpoch, f, 1000, true);
    check(diagShouldSend(false, 0, lastFaults, lastEpoch, 2000, true, MIN),
          "reported fault clears -> all-clear due");
    diagMarkSent(&lastFaults, &lastEpoch, 0, 2000, true);
    check((lastFaults & DIAG_CLEAR_SENT) != 0 && (lastFaults & 0x7FFFu) == f,
          "clear mark sets the marker and keeps the latched bit");
    check(!diagShouldSend(false, 0, lastFaults, lastEpoch, 3000, true, MIN),
          "second clear cycle -> no repeat");

    // Flap: the fault returns after its clear. NOT a new bit -> daily path only.
    check(!diagShouldSend(false, f, lastFaults, lastEpoch, 2000 + 3600, true, MIN),
          "returning fault within the day -> suppressed (no flap storm)");
    check(diagShouldSend(false, f, lastFaults, lastEpoch, 2000 + MIN, true, MIN),
          "returning fault after a day -> re-alert");
    diagMarkSent(&lastFaults, &lastEpoch, f, 2000 + MIN, true);
    check((lastFaults & DIAG_CLEAR_SENT) == 0,
          "the re-alert re-baselines and drops the clear marker");
    check(diagShouldSend(false, 0, lastFaults, lastEpoch, 2000 + MIN + 3600, true, MIN),
          "next clear -> a new all-clear (one per episode, at most two frames/day flapping)");

    // A DIFFERENT fault arriving while clear-marked is still prompt.
    uint16_t lf2 = (uint16_t)(f | DIAG_CLEAR_SENT); uint32_t le2 = 5000;
    check(diagShouldSend(false, DIAG_FAULT_LOW_BATTERY, lf2, le2, 6000, true, MIN),
          "new distinct fault after an all-clear -> prompt");
    diagMarkSent(&lf2, &le2, DIAG_FAULT_LOW_BATTERY, 6000, true);
    check((lf2 & DIAG_CLEAR_SENT) == 0 && (lf2 & 0x7FFFu) == (uint16_t)(f | DIAG_FAULT_LOW_BATTERY),
          "edge send accumulates bits and drops the marker");
  }

  // -------------------------------------------------------------------------
  // 6. Verbose DEV frame: the DEV gate and the encoder byte map.
  // -------------------------------------------------------------------------
  // Gate: DEV-only, once at boot, then every interval.
  check(!verboseShouldSend(false, false, 0, 0, 3600000u), "verbose: PROD never sends (not booted)");
  check(!verboseShouldSend(false, true, 10000000u, 0, 3600000u), "verbose: PROD never sends (elapsed)");
  check(verboseShouldSend(true, false, 0, 0, 3600000u), "verbose: DEV first cycle sends");
  check(!verboseShouldSend(true, true, 1000000u, 1000000u, 3600000u), "verbose: DEV within interval suppressed");
  check(!verboseShouldSend(true, true, 1000000u + 3599999u, 1000000u, 3600000u), "verbose: DEV just before interval suppressed");
  check(verboseShouldSend(true, true, 1000000u + 3600000u, 1000000u, 3600000u), "verbose: DEV at interval sends");
  // millis() wrap: now has wrapped past 0, elapsed is still correct via unsigned math.
  check(verboseShouldSend(true, true, 100u, 0xFFFFFFFFu - 1000u, 3600000u) == false,
        "verbose: wrap, only ~1100ms elapsed -> suppressed");
  check(verboseShouldSend(true, true, 3600000u, 0xFFFFFFFFu - 1000u, 3600000u),
        "verbose: wrap, >=interval elapsed -> sends");

  // Attempt spacing: due is not the same as allowed. The due-time advances only
  // on success, so a failed attempt stays due continuously, and the evaluator
  // runs from a busy-wait loop.
  check(verboseRetryAllowed(false, 0, 0, 300000u), "retry: first attempt always allowed");
  check(!verboseRetryAllowed(true, 100000u, 50000u, 300000u), "retry: within backoff suppressed");
  check(verboseRetryAllowed(true, 350000u, 50000u, 300000u), "retry: after backoff allowed");
  check(verboseRetryAllowed(true, 200000u, 0xFFFFFFFFu - 100000u, 300000u),
        "retry: wrap-safe (300s elapsed across the wrap)");

  {
    VerboseSnapshot v{};
    v.isSolar=true; v.isDev=true; v.coldBoot=true; v.clockValid=true; v.ina219Present=true;
    v.bonusActive=true; v.busAmbiguous=false;
    v.resetCause=0x40; v.bootCounter=3; v.intervalIndex=4; v.seasonState=2; v.voltageBand=1;
    v.batteryMv=4209; v.panelBusMv=5070; v.panelCurrentTenthMa=125; v.sunEwma255=31;
    v.harvestMah=1234; v.ina219Config=0x399F; v.ds18Count=1; v.surfaceTempCenti=2160; v.faults=0;
    v.uptimeSeconds=90061; v.cycleCount=25; v.ramCount=4; v.uplinkCounter=9;
    // panelStats left null: stats bytes must be zero and the info bit clear.
    uint8_t buf[DIAG_VERBOSE_LEN];
    uint8_t n = diagEncodeVerbose(buf, &v);
    check(n == DIAG_VERBOSE_LEN, "verbose: length is 37 (schema 3)");
    check(buf[0]==DIAG_VERBOSE_SCHEMA, "verbose byte0 schema");
    check(buf[1]==(DIAG_INFO_SOLAR|DIAG_INFO_DEV|DIAG_INFO_COLD_BOOT|DIAG_INFO_CLOCK_VALID|
                   DIAG_INFO_INA219_SEEN|DIAG_INFO_BONUS_ACTIVE),
          "verbose byte1 info bits (bonus set, bus-ambig clear)");
    check(buf[2]==0x40, "verbose byte2 reset cause");
    check(buf[3]==3, "verbose byte3 boot counter");
    check(buf[4]==4, "verbose byte4 interval index");
    check(buf[5]==(uint8_t)((2&3)|((1&3)<<2)), "verbose byte5 season|band");
    check(((buf[6]<<8)|buf[7])==4209, "verbose battery mV");
    check(((buf[8]<<8)|buf[9])==5070, "verbose panel bus mV");
    check(((buf[10]<<8)|buf[11])==125, "verbose panel current (0.1mA)");
    check(buf[12]==31, "verbose sun ewma");
    check(((buf[13]<<8)|buf[14])==1234, "verbose harvest mAh");
    check(((buf[15]<<8)|buf[16])==0x399F, "verbose ina219 config");
    check(buf[17]==1, "verbose ds18 count");
    check((int16_t)((buf[18]<<8)|buf[19])==2160, "verbose surface temp centi");
    check(((buf[20]<<8)|buf[21])==0, "verbose faults (all clear)");
    // ---- schema 2 bytes ----
    check(((uint32_t)buf[22]<<24|(uint32_t)buf[23]<<16|(uint32_t)buf[24]<<8|buf[25])==90061u,
          "verbose bytes22-25: uptime 90061 s (25h01m01s) big-endian");
    check(((buf[26]<<8)|buf[27])==25, "verbose bytes26-27: cycle count");
    check(buf[28]==((4<<4)|9), "verbose byte28: ramCount high nibble | uplinkCounter low");
    check(buf[29]==0 && buf[30]==0 && buf[31]==0 && buf[32]==0 && buf[33]==0,
          "verbose: null panelStats -> stats bytes zero");
    check(!(buf[1] & DIAG_INFO_PANEL_STATS), "verbose: null panelStats -> info bit clear");
    // Schema 3: the zero-initialised snapshot encodes the unofficial-build
    // sentinel; a real hash round-trips its three bytes big-endian.
    check(buf[34]==0 && buf[35]==0 && buf[36]==0,
          "verbose bytes34-36: no hash -> 0x000000 (unofficial build)");
  }
  {
    VerboseSnapshot v{}; v.gitHash24 = 0xABC123;
    uint8_t buf[DIAG_VERBOSE_LEN]; diagEncodeVerbose(buf, &v);
    check(buf[34]==0xAB && buf[35]==0xC1 && buf[36]==0x23,
          "verbose bytes34-36: git hash 0xABC123 big-endian");
  }
  { // invalid temp sentinel round-trips as 0x7FFF
    VerboseSnapshot v{}; v.surfaceTempCenti=VERBOSE_TEMP_INVALID;
    uint8_t buf[DIAG_VERBOSE_LEN]; diagEncodeVerbose(buf, &v);
    check((int16_t)((buf[18]<<8)|buf[19])==VERBOSE_TEMP_INVALID, "verbose temp sentinel 0x7FFF");
  }

  // -------------------------------------------------------------------------
  // 7. Panel-profile accumulator (schema 2) and its encoding.
  // -------------------------------------------------------------------------
  {
    PanelStats s; panelStatsInit(&s);
    check(s.n == 0, "stats: fresh accumulator is empty");

    // The measured 2026-07-28 evening: 22 -> 13.8 -> 11.6 -> 2.4 mA.
    panelStatsAdd(&s, 3916, 22.0f);
    panelStatsAdd(&s, 3804, 13.8f);
    panelStatsAdd(&s, 3704, 11.6f);
    panelStatsAdd(&s, 3624, 2.4f);
    check(s.n == 4, "stats: four samples counted");
    check(s.iMinMa == 2.4f && s.iMaxMa == 22.0f, "stats: current min/max tracked");
    check(s.vMinMv == 3624 && s.vMaxMv == 3916, "stats: bus min/max tracked");
    float mean = panelStatsMeanMa(&s);
    check(mean > 12.4f && mean < 12.5f, "stats: mean = 49.8/4 = 12.45 mA");

    // Negative current (reverse leakage artefact) clamps to 0 in the profile.
    PanelStats t; panelStatsInit(&t);
    panelStatsAdd(&t, 3600, -0.5f);
    check(t.iMinMa == 0.0f && t.iMaxMa == 0.0f, "stats: negative current clamps to 0");

    // Encoders: same conventions as the data payload.
    check(panelStatsEncodeMa(12.45f) == 25, "encode: 12.45 mA -> 25 (0.5 mA/LSB)");
    check(panelStatsEncodeMa(200.0f) == 255, "encode: current clamps at 255");
    check(panelStatsEncodeMv(3916) == 130, "encode: 3916 mV -> 130 (30 mV/LSB)");
    check(panelStatsEncodeMv(60000) == 255, "encode: bus clamps at 255");

    // Wired into the frame: stats present -> bit set, bytes populated.
    VerboseSnapshot v{}; v.panelStats = &s;
    uint8_t buf[DIAG_VERBOSE_LEN]; diagEncodeVerbose(buf, &v);
    check((buf[1] & DIAG_INFO_PANEL_STATS) != 0, "frame: stats present -> info bit set");
    check(buf[29]==5 && buf[30]==25 && buf[31]==44, "frame: i min/mean/max = 2.4/12.45/22 mA");
    check(buf[32]==120 && buf[33]==130, "frame: v min/max = 3624/3916 mV");

    // After the reset that follows a successful TX, the next frame is clean.
    panelStatsInit(&s);
    diagEncodeVerbose(buf, &v);
    check(!(buf[1] & DIAG_INFO_PANEL_STATS), "frame: reset accumulator -> bit clear again");
  }

  std::printf("\n%s\n\n", failures ? "FAILED" : "all passed");
  return failures ? 1 : 0;
}
