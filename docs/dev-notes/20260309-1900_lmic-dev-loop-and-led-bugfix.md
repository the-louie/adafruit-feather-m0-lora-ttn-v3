# LMIC dev loop and LED bugfix

**DEV sleep loop:** The development-mode sleep in `loop()` now calls `os_runloop_once()` and `delay(1)` inside the wait loop instead of `delay(10)` only. That keeps the LMIC stack running during the simulated sleep period so RX windows, MAC command processing, and duty-cycle enforcement continue. Previously the blocking delay starved LMIC and could freeze the stack, miss downlinks, and drop the network connection.

**LED during TX:** Onboard LED activation during the blocking wait for `EV_TXCOMPLETE` in `transmitBatchAndWait()` is now conditional on `runMode == 1` (DEV). In production the LED stays off during the 1–2 second TX window to avoid unnecessary battery drain (about 5–10 mA when on).
