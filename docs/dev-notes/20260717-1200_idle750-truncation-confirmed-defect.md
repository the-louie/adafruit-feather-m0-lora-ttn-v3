# `LowPower.idle(750)` does not wait — confirmed defect (S01-10)

## Summary

`ArduinoLowPower::setAlarmIn()` truncates sub-second durations to a zero-second alarm. `LowPower.idle(750)` therefore returns early instead of waiting, the DS18B20 has not finished converting, and the read returns the **previous** conversion. **Every PROD temperature reading is lagged one wake interval.**

PROD-only. The DEV path spends the window in an `os_runloop_once()` loop, so the defect does not exist there — which is why bench testing could never have found it.

## Evidence — from library source, not inference

MCCI-adjacent, `arduino-libraries/ArduinoLowPower`, `src/samd/ArduinoLowPower.cpp`:

```c
void ArduinoLowPowerClass::setAlarmIn(uint32_t millis) {
	if (!rtc.isConfigured()) { attachInterruptWakeup(RTC_ALARM_WAKEUP, NULL, (irq_mode)0); }
	uint32_t now = rtc.getEpoch();
	rtc.setAlarmEpoch(now + millis/1000);      // 750/1000 == 0
	rtc.enableAlarm(rtc.MATCH_YYMMDDHHMMSS);
}
```

Integer division. No sub-second handling. `idle(750)` sets the alarm to the *current* second and `__WFI()`s.

`idle()` clears `SLEEPDEEP`, sets `PM->SLEEP.reg = 2` (IDLE2), and — unlike `sleep()` — does **not** disable SysTick.

## Which branch: confirmed from field data

Two outcomes were possible depending on whether SysTick is gated in IDLE2:

- **returns after ~1 ms** → the lag described above, or
- **blocks until the alarm matches again** (next year) → PROD never transmits at all.

**The early-return branch is confirmed.** Both deployed units transmit continuously — `gisebo-01` f_cnt 376→397, `gisebo-04` 709→837 across the full retained TTN window. Nothing is hanging. So the lag is live.

## What could NOT be checked, and why

Recorded so nobody re-derives a check that cannot run:

- **"252 in slot 0 of a post-reboot uplink"** — needs a reboot. Re-checked 2026-07-17 against the *entire* retained window (139 uplinks, 2026-07-15→17, everything TTN keeps): **zero f_cnt resets on either device.** Both have run uninterrupted for the whole retained history. No reboot has occurred to observe.
- **"252s on FPort 10 that FPort 20 never shows"** — needs a DEV unit. **There are no FPort 20 uplinks at all.**

This is absence of evidence, not evidence of absence. The mechanism is certain from source; only the field signature is unobserved.

## Consequence for the fix

The **bench** is the definitive test (S06-13: flash pre-fix firmware, cold boot, look for 252 in slot 0). The field alarm (S01-08) is insurance on a **closing window**: `gisebo-01` is frozen and then retired, so it keeps pre-fix firmware forever and is the only device that can ever show the signature in production. TTN retains ~3 days. If the alarm is not live when its next natural reboot happens, that chance is gone.

## Cross-reference

`docs/dev-notes/20260309-1700_usb-serial-stability-lowpower-idle-by-runmode.md` describes the symptom — "suspending the CPU and causing the serial connection to reset on every measurement" — and works it around **for DEV only**. That was `idle(750)` gating the APB clock and dropping USB. The USB symptom was diagnosed correctly; the truncation underneath was never suspected, and PROD kept the broken path.

## Fix

Decided: **`delay(750)`**. Safe here specifically — the radio is idle during sensor conversion, so the no-`delay()`-near-the-radio rule does not apply. 682 ms of slack around the INA219's ~68 ms averaging window. See S02-01.
