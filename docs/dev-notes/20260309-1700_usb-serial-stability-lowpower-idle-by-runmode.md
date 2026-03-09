# USB serial stability: LowPower.idle by runMode

## Summary

Conditional wait during Dallas temperature conversion in `readAndBufferSensors()`: in PROD (runMode 0) the code uses `LowPower.idle(750)` for power savings; in DEV (runMode 1) it spends the 750 ms in a loop calling `os_runloop_once()` so USB and LMIC stay active and the serial connection does not drop.

## Rationale

In DEV mode the native USB peripheral depends on the SAMD21 clocks. Unconditional `LowPower.idle(750)` was suspending the CPU and causing the serial connection to reset on every measurement. Branching on runMode keeps PROD behavior unchanged (USB is detached there) and keeps DEV responsive by servicing the LMIC runloop instead of idling.

## Verification

- `readAndBufferSensors()` is only called from `loop()` in the operational state; runMode is set once in setup and not changed afterward.
- PROD path is unchanged (single `LowPower.idle(750)` then read). DEV path uses a 750 ms elapsed-time loop with `os_runloop_once()` only; no long `delay()` that could desynchronize LMIC.
- No TODO.md or TODO-summarized.md in the repo; no TODO cleanup required.
