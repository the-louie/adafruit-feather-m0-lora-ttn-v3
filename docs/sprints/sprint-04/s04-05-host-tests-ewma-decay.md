# S04-05 — Host tests EWMA decay

**Estimate:** 2 h
**Backlog item:** TODO #8
**Depends on:** S04-04
**Needs hardware:** no

## Context

The feedback loop between interval and window is the predicted failure mode of this whole design. Test that the time-based decay actually immunises it — that is the entire point of the RTC work.

## Steps

1. Test decay across varied `dt`: 5 min, 30 min, 6 h. Assert the 24 h time constant holds regardless of sampling rate — that is the property that kills the hunting.
2. Test the linear approximation would have been wrong: assert `exp()` and `dt/TAU` diverge measurably at `dt` = 6 h, so nobody 'simplifies' it later.
3. Simulate a day/night cycle at both index 2 and index 7 and assert the EWMA converges to a similar value. **If it does not, the design is wrong** — this is the test that proves the time-based window works.
4. Test a long dark spell decays toward 0, and continuous sun toward 1.

## Done when

- [ ] Time constant holds independent of sampling rate.
- [ ] Day/night convergence equivalent at index 2 and index 7.
- [ ] The linear-approximation error is pinned by a test.
