# S02-16 — Wire the sketch to PowerPolicy

**Estimate:** 2 h
**Backlog item:** TODO #4
**Depends on:** S02-13, S02-15
**Needs hardware:** no

## Context

The integration step. `loop()`'s two-phase flow — commissioning spins `os_runloop_once()`, operational reads/sends/sleeps — does not change.

## Steps

1. Instantiate `PrimaryCellPolicy` unconditionally for now; the probe lands in sprint 03.
2. Route `decideInterval` and `appendPayload` through the interface.
3. Confirm `loop()` is unchanged in structure.
4. Compile; compare flash/RAM against the S02-02 baseline and record the delta.

## Done when

- [ ] Sketch compiles and uses the interface.
- [ ] Flash/RAM delta recorded and justified.
- [ ] Host tests still pass.
