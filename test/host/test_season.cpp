// Host tests for season.h -- the seasonal baseline and its hysteresis.
//
// Pins the properties a refactor would silently break. Two of them are subtle
// enough that nobody would notice by reading the code:
//
//   * the 1 degC hysteresis gaps (16/15 and 8/7), which stop the state flapping
//   * one transition PER CALL, so Summer -> Winter takes two uplinks
//
// Build and run:  test/host/run_tests.sh

#include "../../season.h"
#include <cstdio>
#include <cmath>

static int failures = 0;

static void check(bool ok, const char *what) {
  if (ok) { std::printf("  ok    %s\n", what); }
  else    { std::printf("  FAIL  %s\n", what); failures++; }
}

int main() {
  std::printf("\nseason -- state machine and hysteresis\n");

  // -------------------------------------------------------------------------
  // 1. Each transition fires at its ENTER threshold, not before.
  // -------------------------------------------------------------------------
  check(seasonUpdate(SEASON_MID, 16.0f) == SEASON_SUMMER, "MID -> SUMMER at 16.0");
  check(seasonUpdate(SEASON_MID, 15.9f) == SEASON_MID,     "MID stays at 15.9 (below enter)");
  check(seasonUpdate(SEASON_WINTER, 8.0f) == SEASON_MID,   "WINTER -> MID at 8.0");
  check(seasonUpdate(SEASON_WINTER, 7.9f) == SEASON_WINTER,"WINTER stays at 7.9 (below enter)");

  // -------------------------------------------------------------------------
  // 2. Each transition RELEASES at its LEAVE threshold -- a whole degree lower.
  //    This gap is the entire point: without it a tank sitting on 16.0 would
  //    flap Summer/Mid on sensor noise alone.
  // -------------------------------------------------------------------------
  // NOTE: the condition is `tempC < LEAVE`, so at EXACTLY 15.0 the state HOLDS.
  // 15.0 is inside the gap, not the edge of it. The band is [15.0, 16.0).
  // My first draft of this test asserted the opposite and failed -- the code was
  // right and the test was wrong. Left documented because the off-by-one is easy
  // to re-introduce from the constant name alone.
  check(seasonUpdate(SEASON_SUMMER, 14.9f) == SEASON_MID,   "SUMMER -> MID at 14.9 (below leave)");
  check(seasonUpdate(SEASON_SUMMER, 15.0f) == SEASON_SUMMER,"SUMMER holds AT 15.0 (leave is exclusive)");
  check(seasonUpdate(SEASON_MID, 6.9f) == SEASON_WINTER,    "MID -> WINTER at 6.9 (below leave)");
  check(seasonUpdate(SEASON_MID, 7.0f) == SEASON_MID,       "MID holds AT 7.0 (leave is exclusive)");

  // -------------------------------------------------------------------------
  // 3. The dead zones. A temperature inside a hysteresis gap must not move the
  //    state in EITHER direction -- that is what makes it a gap rather than a
  //    threshold.
  // -------------------------------------------------------------------------
  {
    // The Summer/Mid gap is [15.0, 16.0) -- 15.0 included.
    bool stable = true;
    for (float t = 15.0f; t < 15.95f; t += 0.1f) {
      if (seasonUpdate(SEASON_SUMMER, t) != SEASON_SUMMER) stable = false;
      if (seasonUpdate(SEASON_MID, t)    != SEASON_MID)    stable = false;
    }
    check(stable, "gap [15.0,16.0): both SUMMER and MID hold (no flap)");
  }
  {
    // The Mid/Winter gap is [7.0, 8.0) -- 7.0 included.
    bool stable = true;
    for (float t = 7.0f; t < 7.95f; t += 0.1f) {
      if (seasonUpdate(SEASON_MID, t)    != SEASON_MID)    stable = false;
      if (seasonUpdate(SEASON_WINTER, t) != SEASON_WINTER) stable = false;
    }
    check(stable, "gap [7.0,8.0): both MID and WINTER hold (no flap)");
  }

  // -------------------------------------------------------------------------
  // 4. No flapping when the water sits exactly on a boundary and dithers.
  //    Simulate a tank hovering at 15.5 with +/-0.4 of noise -- inside the gap.
  // -------------------------------------------------------------------------
  {
    uint8_t s = SEASON_SUMMER;
    int changes = 0;
    for (int i = 0; i < 200; i++) {
      float noise = 0.4f * std::sin(i * 1.7f);   // deterministic, no rand()
      uint8_t next = seasonUpdate(s, 15.5f + noise);
      if (next != s) changes++;
      s = next;
    }
    check(changes == 0, "dither at 15.5 +/-0.4 for 200 wakes: zero transitions");
  }

  // -------------------------------------------------------------------------
  // 5. ONE transition per call. This is the property most likely to be
  //    "cleaned up" by someone who thinks it is a bug. It is not.
  // -------------------------------------------------------------------------
  {
    uint8_t s = SEASON_SUMMER;
    s = seasonUpdate(s, 5.0f);
    check(s == SEASON_MID, "Summer + 5.0C -> MID on the first call (not Winter)");
    s = seasonUpdate(s, 5.0f);
    check(s == SEASON_WINTER, "Summer + 5.0C -> WINTER only on the second call");
  }
  {
    uint8_t s = SEASON_WINTER;
    s = seasonUpdate(s, 25.0f);
    check(s == SEASON_MID, "Winter + 25.0C -> MID on the first call (not Summer)");
    s = seasonUpdate(s, 25.0f);
    check(s == SEASON_SUMMER, "Winter + 25.0C -> SUMMER only on the second call");
  }

  // -------------------------------------------------------------------------
  // 6. Invalid readings must leave the state ALONE. A DS18B20 that has lost its
  //    sensor returns -127.0; a failed read can give NaN. Neither is winter.
  // -------------------------------------------------------------------------
  check(!seasonTempValid(NAN),     "NaN is not a valid temperature");
  check(!seasonTempValid(-127.0f), "-127 (DS18B20 disconnect) is not valid");
  check(!seasonTempValid(85.0f),   "85 (DS18B20 power-on default) is not valid");
  check(seasonTempValid(16.8f),    "16.8 (real gisebo-01 reading) is valid");

  check(seasonUpdate(SEASON_SUMMER, NAN) == SEASON_SUMMER,
        "NaN leaves SUMMER untouched");
  check(seasonUpdate(SEASON_SUMMER, -127.0f) == SEASON_SUMMER,
        "-127 disconnect leaves SUMMER untouched (does NOT fall to Winter)");
  check(seasonUpdate(SEASON_WINTER, 85.0f) == SEASON_WINTER,
        "85 power-on default leaves WINTER untouched (does NOT jump to Summer)");

  // -------------------------------------------------------------------------
  // 7. Base indices.
  // -------------------------------------------------------------------------
  check(seasonBaseIndex(SEASON_SUMMER) == 4, "SUMMER -> index 4 (30 min)");
  check(seasonBaseIndex(SEASON_MID)    == 5, "MID -> index 5 (60 min)");
  check(seasonBaseIndex(SEASON_WINTER) == 7, "WINTER -> index 7 (6 h)");

  // -------------------------------------------------------------------------
  // 8. The production case. gisebo-01 reads 16.8 degC and reports interval
  //    index 4. Anchor the suite to something real.
  // -------------------------------------------------------------------------
  {
    uint8_t s = seasonUpdate(SEASON_SUMMER, 16.8f);
    check(s == SEASON_SUMMER && seasonBaseIndex(s) == 4,
          "production: gisebo-01 at 16.8C -> SUMMER, base index 4");
  }

  std::printf("\n%s\n\n", failures ? "FAILED" : "all passed");
  return failures ? 1 : 0;
}
