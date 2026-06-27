# S03-06 — Save and restore the RTC epoch across reset

**Estimate:** 2 h
**Backlog item:** TODO #5
**Depends on:** S03-04
**Needs hardware:** no

## Context

The SAMD21 RTC has no backup domain, so `NVIC_SystemReset()` at `:404` resets it along with everything else — losing wall clock even though `.noinit` survives.

The join-failure path sleeps 15 minutes *then* resets, so the epoch can be captured after the sleep and before the reset.

## Steps

1. Read `rtc.getEpoch()` after the 15-minute `deepSleep`, immediately before `NVIC_SystemReset()`.
2. Stash it in the `.noinit` struct.
3. On boot, if state is valid and the clock was valid, restore the epoch **as stored — do not add the sleep back**. The epoch is read *after* the `deepSleep` returns, so the RTC has already counted those 15 minutes into it. Adding them again would put the clock 15 minutes fast on every join-failure reset. (An earlier draft of this task said to add them; that was wrong.) The only genuinely uncounted time is between the epoch read and the RTC being reconfigured after reboot — milliseconds, and below the resolution of anything that consumes the clock.
4. Keep the clock-valid flag honest: a restored epoch is still valid, a cold-booted one is not.

## Done when

- [ ] Epoch survives the join-failure reset path.
- [ ] The stored epoch is restored verbatim; the 15 minutes are NOT added back.
- [ ] Clock-valid semantics stay correct across the reset.
