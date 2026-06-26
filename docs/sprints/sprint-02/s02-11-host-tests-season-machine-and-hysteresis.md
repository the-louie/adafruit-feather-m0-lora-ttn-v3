# S02-11 — Host tests season machine and hysteresis

**Estimate:** 2 h
**Backlog item:** TODO #4
**Depends on:** S02-10
**Needs hardware:** no

## Context

The hysteresis is subtle and the state machine has a property worth pinning: transitions are an `else if` chain, so it steps **one level per call**. Summer → Winter takes two uplinks. That is intended, but it is exactly the kind of thing a later refactor breaks silently.

## Steps

1. Test each transition with its hysteresis gap (16/15 °C and 8/7 °C).
2. Test that no flapping occurs when the temperature sits exactly on a boundary.
3. Test the one-step-per-call property explicitly.
4. Test that NaN and out-of-range (e.g. the DS18B20's -127 disconnect value) leave the state untouched.
5. Test the cold-start path: `setup()` starts at Summer, so a winter boot takes two uplinks to settle.

## Done when

- [ ] All four transitions covered with hysteresis.
- [ ] One-step-per-call pinned.
- [ ] -127 disconnect value covered.
