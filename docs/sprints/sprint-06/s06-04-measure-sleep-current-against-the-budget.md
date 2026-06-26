# S06-04 — Measure sleep current against the budget

**Estimate:** 2 h
**Backlog item:** TODO #11
**Depends on:** S06-01
**Needs hardware:** YES

## Context

The whole energy model rests on an inference: from the brief's own figures, a 12× interval change moves consumption 31%, which backs out to ~290 µA of board quiescent. That was never measured.

If it is right, the interval ladder saves under 4% and the primary variant lasts about a year on 4×AA. If it is wrong, several decisions deserve revisiting.

## Steps

1. Measure deep-sleep current directly.
2. Break it down: regulator, charger, INA219 power-down, supercap leakage, PCM.
3. Compare against the ~290 µA inference and against the brief's 7.21 mAh/day winter figure.
4. If it really is ~290 µA, quiescent surgery beats any interval tuning — say so with numbers.

## Done when

- [ ] Sleep current measured and broken down.
- [ ] The ~290 µA inference confirmed or corrected.
- [ ] Implications for the primary variant's life stated.
