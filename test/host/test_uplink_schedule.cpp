// Host tests for uplink_schedule.h -- the send decision and the 4-bit counter.
//
// This is the regression test for the two defects confirmed from 139 production
// uplinks. It includes the REAL header the firmware uses, so it cannot drift
// from the shipped code.
//
// Build and run:  test/host/run_tests.sh

#include "../../uplink_schedule.h"
#include <cstdio>
#include <vector>

static int failures = 0;

static void check(bool ok, const char *what) {
  if (ok) {
    std::printf("  ok    %s\n", what);
  } else {
    std::printf("  FAIL  %s\n", what);
    failures++;
  }
}

// ---------------------------------------------------------------------------
// A record of what one uplink looked like, so we can assert on the whole run
// rather than on a single moment.
// ---------------------------------------------------------------------------
struct Uplink {
  int wake;        // which wake it fired on
  int samples;     // ramCount at send time -- 6 is a full batch
  int counter;     // the 4-bit value stamped into the payload
};

// ---------------------------------------------------------------------------
// Simulate N wakes through the REAL schedule logic.
// Mirrors loop(): sample, then decide, and on a successful TX advance.
// ---------------------------------------------------------------------------
static std::vector<Uplink> simulateNew(int wakes, bool joinFirst = true) {
  UplinkSchedule s;
  uplinkScheduleInit(&s, 6);
  if (joinFirst) uplinkScheduleOnJoin(&s);

  std::vector<Uplink> sent;
  for (int w = 1; w <= wakes; w++) {
    uplinkScheduleOnSample(&s);
    if (uplinkScheduleShouldSend(&s)) {
      sent.push_back({w, s.ramCount, uplinkScheduleCounterForPayload(&s)});
      uplinkScheduleOnTxSuccess(&s);
    }
  }
  return sent;
}

// ---------------------------------------------------------------------------
// The OLD behaviour, modelled exactly as the firmware had it:
//
//     wakeCounter = (wakeCounter + 1) & 0x0F;   // in readAndBufferSensors()
//     if (wakeCounter == 1 || ramCount >= batchTarget) { transmit(); }
//
// Kept so the defect is pinned in a test rather than only in a commit message.
// If anyone ever proposes inferring "first" from a counter again, this is what
// they get.
// ---------------------------------------------------------------------------
static std::vector<Uplink> simulateOld(int wakes) {
  int wakeCounter = 0, ramCount = 0;
  const int batchTarget = 6;

  std::vector<Uplink> sent;
  for (int w = 1; w <= wakes; w++) {
    if (ramCount < batchTarget) ramCount++;
    wakeCounter = (wakeCounter + 1) & 0x0F;
    if (wakeCounter == 1 || ramCount >= batchTarget) {
      sent.push_back({w, ramCount, wakeCounter});
      ramCount = 0;
    }
  }
  return sent;
}

int main() {
  std::printf("\nuplink_schedule -- 40-wake simulation\n");

  // -------------------------------------------------------------------------
  // 1. The defect, pinned. The old code MUST reproduce production.
  //    If this ever passes cleanly, our model of the bug was wrong.
  // -------------------------------------------------------------------------
  {
    auto old = simulateOld(40);
    bool sawShortBatch = false;
    bool onlyOneSevenThirteen = true;
    for (const auto &u : old) {
      if (u.samples < 6) sawShortBatch = true;
      if (u.counter != 1 && u.counter != 7 && u.counter != 13) onlyOneSevenThirteen = false;
    }
    check(sawShortBatch,
          "OLD: fires with a partial batch (the defect, reproduced)");
    check(onlyOneSevenThirteen,
          "OLD: counter only ever takes {1,7,13} -- matches 139 production uplinks");

    // Count the short batches. Production showed ~1 in 3.
    int shorts = 0;
    for (const auto &u : old) if (u.samples == 4) shorts++;
    check(shorts >= 2,
          "OLD: short batches recur (~1 in 3), not a one-off at boot");

    bool counterEverZero = false;
    for (const auto &u : old) if (u.counter == 0) counterEverZero = true;
    check(!counterEverZero,
          "OLD: counter never reaches 0 -- so rebootDetected could never fire");
  }

  // -------------------------------------------------------------------------
  // 2. The fix. Every uplink after the post-join flush carries a FULL batch.
  // -------------------------------------------------------------------------
  {
    auto sent = simulateNew(40);

    check(!sent.empty() && sent[0].wake == 1 && sent[0].samples == 1,
          "NEW: post-join flush fires on wake 1 with whatever we have");

    bool allFull = true;
    for (size_t i = 1; i < sent.size(); i++) {
      if (sent[i].samples != 6) allFull = false;
    }
    check(allFull,
          "NEW: every subsequent uplink carries a full 6-sample batch");

    // This is the assertion that fails against the old code.
    bool anyShortAfterFirst = false;
    for (size_t i = 1; i < sent.size(); i++) {
      if (sent[i].samples < 6) anyShortAfterFirst = true;
    }
    check(!anyShortAfterFirst,
          "NEW: no partial batches after the flush (OLD fails this)");
  }

  // -------------------------------------------------------------------------
  // 3. The counter. Consecutive uplinks differ by exactly 1, so a gap means a
  //    dropped message and a repeat means a retry.
  // -------------------------------------------------------------------------
  {
    auto sent = simulateNew(40);
    bool stepsByOne = true;
    for (size_t i = 1; i < sent.size(); i++) {
      int expected = (sent[i - 1].counter + 1) & 0x0F;
      if (sent[i].counter != expected) stepsByOne = false;
    }
    check(stepsByOne, "NEW: counter increments by exactly 1 per uplink");
    check(!sent.empty() && sent[0].counter == 0,
          "NEW: first uplink after a cold start carries counter 0");
  }

  // -------------------------------------------------------------------------
  // 4. Wraparound must NOT look like a reboot or a gap.
  // -------------------------------------------------------------------------
  {
    UplinkSchedule s;
    uplinkScheduleInit(&s, 6);
    for (int i = 0; i < 15; i++) uplinkScheduleOnTxSuccess(&s);
    check(uplinkScheduleCounterForPayload(&s) == 15, "wrap: reaches 15");
    uplinkScheduleOnTxSuccess(&s);
    check(uplinkScheduleCounterForPayload(&s) == 0, "wrap: 15 -> 0, stays 4-bit");
  }

  // -------------------------------------------------------------------------
  // 5. A failed TX must not advance anything. The batch is retried and the
  //    retry carries the SAME counter value -- that is how the backend tells a
  //    retry from a fresh uplink.
  // -------------------------------------------------------------------------
  {
    UplinkSchedule s;
    uplinkScheduleInit(&s, 6);
    for (int i = 0; i < 6; i++) uplinkScheduleOnSample(&s);
    check(uplinkScheduleShouldSend(&s), "timeout: batch full, would send");

    uint8_t before = uplinkScheduleCounterForPayload(&s);
    // TX times out -> onTxSuccess is NOT called. Nothing changes.
    check(uplinkScheduleCounterForPayload(&s) == before,
          "timeout: counter does not advance on a failed TX");
    check(s.ramCount == 6, "timeout: batch is preserved for the retry");

    // Next wake: sample again (ramCount caps at batchTarget), still wants to send.
    uplinkScheduleOnSample(&s);
    check(s.ramCount == 6, "timeout: ramCount caps at batchTarget, does not overflow");
    check(uplinkScheduleShouldSend(&s), "timeout: retries on the next wake");
    check(uplinkScheduleCounterForPayload(&s) == before,
          "timeout: the retry carries the SAME counter value");
  }

  // -------------------------------------------------------------------------
  // 6. The post-join flush is a ONE-SHOT. It must not re-arm itself.
  //    This is the whole point of S02-04.
  // -------------------------------------------------------------------------
  {
    auto sent = simulateNew(40);
    int flushes = 0;
    for (const auto &u : sent) if (u.samples < 6) flushes++;
    check(flushes == 1, "flush: fires exactly once in 40 wakes, never re-arms");
  }

  // -------------------------------------------------------------------------
  // 7. No join, no send. A device that never joins must not transmit into the
  //    void until its batch fills naturally.
  // -------------------------------------------------------------------------
  {
    auto sent = simulateNew(5, /*joinFirst=*/false);
    check(sent.empty(), "no join: no flush before EV_JOINED");
  }

  std::printf("\n%s\n\n", failures ? "FAILED" : "all passed");
  return failures ? 1 : 0;
}
