# RTC, timekeeping, and the ownership seam (S03-06, S03-09, S03-10, S03-12, S03-18)

## The RTC was always there

The SAMD21 RTC counts through standby, and the Feather M0 carries an external 32.768 kHz crystal. The Arduino SAMD variant for this board does not define `CRYSTALLESS`, so `RTCZero` sources GCLK2 from that crystal: ~20-50 ppm, not the wild drift of the internal RC.

**And it was already in use.** `ArduinoLowPower` on SAMD wraps `RTCZero` — `deepSleep()` has always been crystal-backed and accurate. This sprint does not make sleeping better; it makes the clock **readable**. `millis()` genuinely does not advance through `deepSleep` (it is clocked from the main clock, which stops), but that is a fact about `millis()`, not the hardware.

## The ownership seam — the highest-risk unverified thing in the sprint

Two libraries want the same RTC peripheral, and getting this wrong wipes the clock.

`RTCZero::begin(false)` calls `RTCreset()`, which software-resets the peripheral and **clears the counter** — unless `PM->RCAUSE` indicates a SYST/WDT/EXT reset. So a `begin()` called during normal operation, after a power-on reset, would wipe the time. And `_configured` is an *instance* member, so a second `RTCZero` object believes the hardware is unconfigured and will happily reconfigure it.

**The fix: never call `begin()` on our instance.** `getEpoch()` and `setEpoch()` only touch `RTC->MODE2.CLOCK.reg` and do not test `_configured`. So:

- `ArduinoLowPower` owns configuration. We force it once in `setup()` via `attachInterruptWakeup(RTC_ALARM_WAKEUP, nullptr, 0)` — which is exactly what `setAlarmIn()` does lazily, but done explicitly so `getEpoch()` is valid **before** the first sleep. That matters: the first uplink after join, which requests network time, happens before any `deepSleep`.
- Our `rtc` instance is read/write for the clock register only. **`rtc.begin()` is never called** — verified, 0 executable calls.

This still needs hardware to confirm (S06-03). It turns "unverifiable seam" into a specific claim: *sleep timing stays accurate, `getEpoch()` returns sane values across sleeps, and the read instance never disturbs the alarm.*

## It does not survive NVIC_SystemReset()

SAMD21 has no backup domain, so the join-failure reset clears the RTC along with everything else. The epoch is stashed in `.noinit` immediately before the reset and restored on boot.

**Restored verbatim — the 15 minutes are NOT added back.** The join-failure path sleeps 15 minutes, *then* reads the epoch, *then* resets. The RTC counted those minutes into the value we read, so adding them again would put the clock 15 minutes fast on every join-failure reset. (An earlier draft of the plan said to add them; that was wrong, and it is now a comment in the code.)

## DeviceTimeReq — a one-shot, not a dependency

Wall clock is seeded once via `LMIC_requestNetworkTime()` on the first uplink after join, unless a valid clock survived the reset. With this crystal one acquisition holds for months (~4 s/day), so it is a one-shot rather than a standing downlink dependency — which matters, because `LMIC_setLinkCheckMode(0)` deliberately quieted the stack.

Verified against MCCI LMIC v6.0.1 source:

- `LMIC_ENABLE_DeviceTimeReq` **defaults to 1** — nothing to configure.
- The callback is `void cb(void*, int flagSuccess)` and **carries no time**. The time is fetched inside it via `LMIC_getNetworkTimeReference(&ref)`, which fills `{tLocal, tNetwork}`. `tNetwork` is GPS seconds at `tLocal`; we add the elapsed time since `tLocal` (the callback runs after the RX window) or the clock lands seconds slow.

GPS->UTC (315964800 offset, minus 18 leap seconds) and the plausibility gate are in `timekeeping.h`, 12 host assertions, validated against an independently-derived reference pair.

## No clock is a supported state

Until `DeviceTimeReq` lands — or forever, in poor coverage — `clockValid` stays 0. For the **primary** variant nothing consumes the clock, so this is a no-op. The **solar** policy's raw-EWMA fallback (it runs without day-length normalisation when the clock is invalid) arrives in sprint 04. A unit that never acquires a clock is visible via the status byte rather than silently degraded.
