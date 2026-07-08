// Host tests for timekeeping.h -- GPS -> UTC conversion.
//
// The conversion has two constants that are easy to get wrong and invisible
// when wrong: the 315964800 epoch offset and the 18 leap seconds. A one-second
// error here silently skews every clarity ratio the backend computes. So the
// reference pair below was derived INDEPENDENTLY (Python, datetime) rather than
// with these same constants.
//
// Build and run:  test/host/run_tests.sh

#include "../../timekeeping.h"
#include <cstdio>

static int failures = 0;

static void check(bool ok, const char *what) {
  if (ok) { std::printf("  ok    %s\n", what); }
  else    { std::printf("  FAIL  %s\n", what); failures++; }
}

int main() {
  std::printf("\ntimekeeping -- GPS to UTC\n");

  // -------------------------------------------------------------------------
  // 1. The independently-derived reference pair.
  //    2026-07-17 09:33:07 UTC:
  //      GPS seconds = 1468316005
  //      unix UTC    = 1784280787
  //    Derived in Python from datetime, NOT from these constants.
  // -------------------------------------------------------------------------
  {
    uint32_t gps = 1468316005u;
    uint32_t unix = gpsToUnixUtc(gps, 0);
    check(unix == 1784280787u, "reference pair converts exactly (2026-07-17 09:33:07)");
  }

  // -------------------------------------------------------------------------
  // 2. The leap-second correction is applied, not forgotten. If someone drops
  //    it, the result is 18 seconds fast.
  // -------------------------------------------------------------------------
  {
    uint32_t withLeap = gpsToUnixUtc(1468316005u, 0);
    uint32_t withoutLeap = 1468316005u + GPS_UNIX_OFFSET;   // the buggy version
    check(withoutLeap - withLeap == GPS_UTC_LEAP_SECONDS,
          "leap-second correction is 18 s (dropping it lands 18 s fast)");
  }

  // -------------------------------------------------------------------------
  // 3. Elapsed-time compensation. The callback runs seconds after LMIC sampled
  //    the time, so we add the gap. Without it the clock is slow.
  // -------------------------------------------------------------------------
  {
    uint32_t base = gpsToUnixUtc(1468316005u, 0);
    check(gpsToUnixUtc(1468316005u, 2500) == base + 2,
          "2500 ms elapsed adds 2 s (integer seconds)");
    check(gpsToUnixUtc(1468316005u, 999) == base,
          "999 ms elapsed adds 0 s (rounds down, sub-second is below our resolution)");
  }

  // -------------------------------------------------------------------------
  // 4. Plausibility gate. A network-time reply that did not really land must be
  //    rejected rather than written to the RTC as nonsense.
  // -------------------------------------------------------------------------
  check(!utcPlausible(0),          "0 is not a plausible now");
  check(!utcPlausible(100u),       "a tiny value is not plausible");
  check(!utcPlausible(1000000000u),"2001 is before our lower bound, rejected");
  check(utcPlausible(1784280787u), "2026 is plausible");
  check(!utcPlausible(3000000000u),"2065 is after our upper bound, rejected");
  check(utcPlausible(UTC_PLAUSIBLE_MIN), "exactly the lower bound is plausible");
  check(utcPlausible(UTC_PLAUSIBLE_MAX), "exactly the upper bound is plausible");

  // -------------------------------------------------------------------------
  // 5. A converted real reply passes the plausibility gate -- the two agree.
  // -------------------------------------------------------------------------
  {
    uint32_t unix = gpsToUnixUtc(1468316005u, 1200);
    check(utcPlausible(unix), "a real converted reply is plausible");
  }

  std::printf("\n%s\n\n", failures ? "FAILED" : "all passed");
  return failures ? 1 : 0;
}
