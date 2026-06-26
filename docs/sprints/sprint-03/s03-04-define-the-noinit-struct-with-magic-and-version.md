# S03-04 — Define the noinit struct with magic and version

**Estimate:** 2 h
**Backlog item:** TODO #5
**Depends on:** sprint-02
**Needs hardware:** no

## Context

A soft reset does not physically clear SRAM — only the C runtime zeroes `.bss`. So `__attribute__((section(".noinit")))` survives `NVIC_SystemReset()` at zero flash cost, fully consistent with the no-FlashStorage rule in master-plan.

**The layout version is not optional.** A firmware upgrade that changes the struct must not read the old layout as valid, or the device resumes from garbage — strictly worse than a cold boot.

## Steps

1. `persist.h`: struct with magic word + layout version + payload.
2. Contents: season state, `currentIntervalIndex`, `lastTempC`, uplink counter, boot counter, RTC epoch, sun EWMA, harvest accumulator.
3. Validity: magic match AND version match. Either failing → cold boot.
4. Document the rule loudly: **bump the version on any field change.**

## Done when

- [ ] Struct defined with magic and version.
- [ ] Validity requires both.
- [ ] The bump-on-change rule is documented where it will be seen.
