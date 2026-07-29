// Host tests for variant_probe.h -- the probe DECISION logic.
//
// The physical I2C is in the .ino; the judgement is here. The failure path
// matters more than the success path: a solar board that boots the primary
// policy silently parks itself at a 7-day interval, so the decision must fail
// SAFE and be catchable.
//
// Build and run:  test/host/run_tests.sh

#include "../../variant_probe.h"
#include <cstdio>

static int failures = 0;

static void check(bool ok, const char *what) {
  if (ok) { std::printf("  ok    %s\n", what); }
  else    { std::printf("  FAIL  %s\n", what); failures++; }
}

static ProbeResult present() {
  return {true, true, INA219_CONFIG_RESET_VALUE, true, INA219_CAL_RESET_VALUE};
}
static ProbeResult absent() {
  return {false, false, 0, false, 0};
}
static ProbeResult ackButWrongConfig() {
  return {true, true, 0x1234, true, 0};   // something else at 0x40
}
static ProbeResult ackButNoRead() {
  return {true, false, 0, false, 0};      // ACKs, then the bus hangs mid-read
}

int main() {
  std::printf("\nvariant_probe -- decision logic\n");

  // -------------------------------------------------------------------------
  // 1. Single-attempt recognition.
  // -------------------------------------------------------------------------
  {
    ProbeResult p = present();
    check(probeAttemptFoundIna219(&p), "clean present -> found");
    ProbeResult a = absent();
    check(!probeAttemptFoundIna219(&a), "absent -> not found");
    ProbeResult w = ackButWrongConfig();
    check(!probeAttemptFoundIna219(&w),
          "ACK but wrong config -> NOT found (something else at 0x40)");
    ProbeResult n = ackButNoRead();
    check(!probeAttemptFoundIna219(&n),
          "ACK but no config read -> NOT found (flaky bus, not a sensor)");

    // A present-but-already-calibrated INA219 (warm reset holds 0x019F from
    // setCalibration_16V_400mA) is NOT the reset value, so the DECISION rejects
    // it -- which is exactly why probeIna219Once() must soft-reset the sensor to
    // 0x399F before reading. Regression guard for the gisebo-05 2026-07-27
    // misdetect (solar booted PRIMARY after an RST-button reset).
    ProbeResult cal = { true, true, INA219_CONFIG_CALIBRATED_VALUE, true, 0x2000 };
    check(!probeAttemptFoundIna219(&cal),
          "calibrated 0x019F is not 0x399F -> the .ino must soft-reset before reading");
    ProbeResult afterReset = { true, true, INA219_CONFIG_RESET_VALUE, true, 0x0000 };
    check(probeAttemptFoundIna219(&afterReset),
          "after soft-reset the INA219 reads 0x399F -> recognised");

    // The 32-bit identity (item 20): after RST, Calibration (05h) must be
    // 0x0000 too. Config alone matching is no longer enough -- guards against
    // a foreign device at 0x40 that happens to serve 0x399F at register 00h.
    ProbeResult calWrong = { true, true, INA219_CONFIG_RESET_VALUE, true, 0x1234 };
    check(!probeAttemptFoundIna219(&calWrong),
          "config right but calibration nonzero after RST -> NOT found");
    ProbeResult calUnread = { true, true, INA219_CONFIG_RESET_VALUE, false, 0 };
    check(!probeAttemptFoundIna219(&calUnread),
          "config right but calibration unreadable -> NOT found (flaky bus)");
  }

  // -------------------------------------------------------------------------
  // 2. Present INA219 -> SOLAR.
  // -------------------------------------------------------------------------
  {
    ProbeResult attempts[3] = {present(), present(), present()};
    check(probeDecide(attempts, 3) == VARIANT_SOLAR, "3x present -> SOLAR");
  }

  // -------------------------------------------------------------------------
  // 3. Absent INA219 -> PRIMARY.
  // -------------------------------------------------------------------------
  {
    ProbeResult attempts[3] = {absent(), absent(), absent()};
    check(probeDecide(attempts, 3) == VARIANT_PRIMARY, "3x absent -> PRIMARY");
  }

  // -------------------------------------------------------------------------
  // 4. THE ONE THAT MATTERS. A real sensor that glitches on some attempts must
  //    still be detected. Retrying favours "present" precisely because missing a
  //    present INA219 is the catastrophic outcome (silent 7-day interval).
  // -------------------------------------------------------------------------
  {
    ProbeResult a[3] = {absent(), present(), absent()};   // one clean read in three
    check(probeDecide(a, 3) == VARIANT_SOLAR,
          "one clean read among glitches -> SOLAR (do not miss a present sensor)");
    ProbeResult b[3] = {absent(), absent(), present()};   // only the last succeeds
    check(probeDecide(b, 3) == VARIANT_SOLAR, "last-attempt success -> SOLAR");
  }

  // -------------------------------------------------------------------------
  // 5. A bus that ACKs 0x40 every time but never reads clean config stays
  //    PRIMARY. This is the "hung bus" / "wrong device" case -- three ACKs are
  //    not evidence of an INA219 if the config never verifies.
  // -------------------------------------------------------------------------
  {
    ProbeResult a[3] = {ackButNoRead(), ackButNoRead(), ackButNoRead()};
    check(probeDecide(a, 3) == VARIANT_PRIMARY, "3x ACK-no-read -> PRIMARY (hung bus)");
    ProbeResult b[3] = {ackButWrongConfig(), ackButWrongConfig(), ackButWrongConfig()};
    check(probeDecide(b, 3) == VARIANT_PRIMARY, "3x ACK-wrong-config -> PRIMARY");
  }

  // -------------------------------------------------------------------------
  // 6. The misdetect backstop. A solar board that fell through to PRIMARY reads
  //    li-ion voltages, which are impossible for a real 6 V primary pack.
  // -------------------------------------------------------------------------
  {
    // Real fleet values must NOT trip it.
    check(!primaryVbatImplausible(VARIANT_PRIMARY, 5.768f),
          "gisebo-01 at 5.768 V on PRIMARY is plausible (no alarm)");
    check(!primaryVbatImplausible(VARIANT_PRIMARY, 5.233f),
          "gisebo-04 at 5.233 V on PRIMARY is plausible (no alarm)");
    // A misdetected solar board reads li-ion.
    check(primaryVbatImplausible(VARIANT_PRIMARY, 4.1f),
          "PRIMARY reading 4.1 V (li-ion) is IMPLAUSIBLE -> misdetect");
    check(primaryVbatImplausible(VARIANT_PRIMARY, 3.7f),
          "PRIMARY reading 3.7 V (li-ion plateau) is IMPLAUSIBLE -> misdetect");
    // On the SOLAR variant, low voltage is normal -- never flag it.
    check(!primaryVbatImplausible(VARIANT_SOLAR, 3.7f),
          "SOLAR reading 3.7 V is normal, not a misdetect");
    // The empty band between 4.2 (li-ion max) and 4.5: nothing legitimate lands
    // here, which is why the threshold cannot false-fire.
    check(primaryVbatImplausible(VARIANT_PRIMARY, 4.49f),
          "4.49 V on PRIMARY is still implausible (below the 4.5 floor)");
  }

  std::printf("\n%s\n\n", failures ? "FAILED" : "all passed");
  return failures ? 1 : 0;
}
