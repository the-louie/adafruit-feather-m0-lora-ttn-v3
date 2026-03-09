# Network MAC command processing timing

## Summary

The post-join LED blink (5 blinks, 50 ms on / 50 ms off) was changed from blocking `delay(50)` to non-blocking waits that call `os_runloop_once()` for each 50 ms segment. Only the main .ino was modified.

## Rationale

The network server often sends MAC commands (e.g. LinkADR, NewChannelReq, RXParamSetupReq) immediately after join. While the device was in the blocking blink (500 ms total), the LMIC event loop never ran, so the radio could not receive or process these downlinks. Using `millis()`-based loops with `os_runloop_once()` keeps the same visual blink but allows LMIC to process MAC commands during the blink, avoiding missed downlinks and unstable connectivity.

## Verification

- Same behavior: 5 iterations, 50 ms on, 50 ms off; `joinSuccessBlinkPending` cleared at the start of the block. No new globals or helpers.
- Pattern matches existing code: `transmitBatchAndWait()` uses `millis()` + `while` + `os_runloop_once()`; post-join blink now uses the same pattern so LMIC runs during the blink.
- No TODO.md or TODO-summarized.md in the repo; no TODO cleanup required.
