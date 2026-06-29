#pragma once
//
// When to uplink, and what counter to stamp on it.
//
// Pure logic, no Arduino dependencies, so the host tests can exercise the SAME
// code the firmware runs rather than a copy that drifts from it. That matters
// here more than usual: the two defects this replaces both survived because
// nothing executable ever checked them.
//
// ---------------------------------------------------------------------------
// What was wrong before
// ---------------------------------------------------------------------------
//
// The old send decision was:
//
//     if (wakeCounter == 1 || ramCount >= batchTarget) { transmit(); }
//
// `wakeCounter == 1` was meant as "the first uplink after joining". It is not.
// wakeCounter is 4-bit, so it WRAPS TO 1 EVERY 16 WAKES and re-fires with a
// partial batch. Confirmed across 139 production uplinks on both devices: the
// sequence takes only {1, 7, 13}, and seq 1 always carries exactly 4 samples.
// The inter-uplink gaps show it too -- only ever 4x or 6x the interval.
// A third of every uplink ever sent carried a short batch and two dead bytes.
//
// The same field was also read as reboot detection (`sequence === 0`). It never
// fired once, and could not: a free-running 4-bit counter cannot distinguish a
// reboot from a wraparound IN PRINCIPLE. Reboot detection was free all along --
// LMIC_reset() clears seqnoUp, so every reboot restarts f_cnt in TTN metadata.
//
// ---------------------------------------------------------------------------
// What this does instead
// ---------------------------------------------------------------------------
//
//   * "first uplink after join" is an explicit flag. Never inferred from a
//     counter that wraps.
//   * The 4 bits count SUCCESSFUL UPLINKS. Consecutive uplinks then differ by
//     exactly 1, so the backend can read:
//         +1        normal
//         repeat    a TX timed out and the batch was retried
//         gap       an uplink was genuinely lost
//
#include <stdint.h>

#define UPLINK_COUNTER_MASK 0x0F

struct UplinkSchedule {
  uint8_t ramCount;              // samples buffered, 0..batchTarget
  uint8_t batchTarget;
  uint8_t uplinkCounter;         // 4-bit, goes on the wire
  bool    firstUplinkAfterJoin;
};

// Cold start. firstUplinkAfterJoin stays false until EV_JOINED says otherwise.
inline void uplinkScheduleInit(UplinkSchedule *s, uint8_t batchTarget) {
  s->ramCount = 0;
  s->batchTarget = batchTarget;
  s->uplinkCounter = 0;
  s->firstUplinkAfterJoin = false;
}

// Call from EV_JOINED. Arms the one-shot flush of whatever we have.
inline void uplinkScheduleOnJoin(UplinkSchedule *s) {
  s->firstUplinkAfterJoin = true;
}

// Call once per wake, after a sample is buffered.
inline void uplinkScheduleOnSample(UplinkSchedule *s) {
  if (s->ramCount < s->batchTarget) {
    s->ramCount++;
  }
}

inline bool uplinkScheduleShouldSend(const UplinkSchedule *s) {
  return s->firstUplinkAfterJoin || s->ramCount >= s->batchTarget;
}

// The value that goes in the payload. Stamped BEFORE the TX, so a successful
// uplink carries the count of uplinks that preceded it: 0, 1, 2, ...
inline uint8_t uplinkScheduleCounterForPayload(const UplinkSchedule *s) {
  return s->uplinkCounter & UPLINK_COUNTER_MASK;
}

// Call ONLY on EV_TXCOMPLETE. Not on a timeout.
//
// On timeout none of this runs, so ramCount is preserved and the batch is
// retried on the next wake carrying the SAME counter value -- which is how the
// backend tells a retry from a fresh uplink.
inline void uplinkScheduleOnTxSuccess(UplinkSchedule *s) {
  s->uplinkCounter = (uint8_t)((s->uplinkCounter + 1) & UPLINK_COUNTER_MASK);
  s->ramCount = 0;
  s->firstUplinkAfterJoin = false;
}
