# S06-06 — Verify noinit survives a real reset

**Estimate:** 2 h
**Backlog item:** TODO #5
**Depends on:** S06-01
**Needs hardware:** YES

## Context

`.noinit` surviving `NVIC_SystemReset()` rests on a claim about SAMD21 SRAM and the C runtime: a soft reset does not physically clear RAM, only startup code zeroes `.bss`. That reasoning is sound but has never been run.

## Steps

1. Force the join-failure path (deny the join) and confirm the reset happens.
2. Confirm season state, counters, EWMA, and harvest all survive.
3. Confirm the RTC epoch is restored, plus the 15 minutes slept.
4. Confirm a **power cycle** correctly reads as a cold boot — RAM decays, but not instantly. Check a brief power interruption does not produce a false 'valid' magic word. This is the nastiest case and the most likely to surprise.

## Done when

- [ ] State survives a soft reset.
- [ ] Epoch restored correctly.
- [ ] A brief power interruption cannot produce a false restore.
