# Fix: revert `idle(750)` to `delay(750)` (S02-01, S02-03)

## Change

`readAndBufferSensors()`: PROD waits with `delay()` for the remainder of the Dallas conversion window instead of `LowPower.idle(750)`.

This is `aad7bca` (2026-03-09 12:03) reverted. That commit replaced a working `delay(750)` with `LowPower.idle(750)` as a power optimisation. `ArduinoLowPower::setAlarmIn()` does `rtc.setAlarmEpoch(now + millis/1000)` — integer division — so `idle(750)` truncates to a zero-second alarm and returns immediately. The DS18B20 has not converted, and the read returns the **previous** conversion. Every PROD reading has been lagged one wake interval since.

## Why `delay()` is acceptable here specifically

The master-plan rule is "no `delay()` while the radio is active" — it desynchronises the MAC layer. **The radio is idle during sensor conversion.** No TX is in flight, no RX window is open. The rule does not apply, and the comment in the code says so, because otherwise someone will optimise this back.

The power argument for `idle()` was never real: quiescent draw (~290 µA) dominates the budget, so 750 ms of run-mode current per wake is noise. The optimisation traded correct data for a saving that does not exist.

## Not a literal revert — the window is now measured properly

```c
uint32_t convStart = millis();
sensors.requestTemperatures();
// ... S04-02 will read the INA219 here (~68 ms of averaging) ...
uint32_t elapsed = millis() - convStart;
if (elapsed < DALLAS_CONV_MS) delay(DALLAS_CONV_MS - elapsed);
```

The original `delay(750)` sat immediately after `requestTemperatures()` with nothing between. The solar policy will read the INA219 inside this window, and a naive `delay(750)` after that read would make the wake **818 ms**, not 750. Measuring elapsed time from `convStart` makes the wait shrink to fit rather than the wake grow.

The DEV branch now measures from `convStart` too, for consistency. Its behaviour is unchanged — it was never affected by the defect, which is exactly why bench testing could never have found it.

## Verification

No hardware. Three checks:

1. **Compiles.** 61588 B, down 44 B from the S01-00 baseline of 61632 — the RTC alarm path was *costing* flash to be wrong.
2. **No executable `idle()` call survives.** `gcc -fpreprocessed` comment-strip → 0 occurrences. The only textual match is the warning comment.
3. **Window arithmetic proven in isolation** across five cases:

| work in window | wait | total wake | |
|---|---|---|---|
| 0 ms | 750 | 750 | today |
| 68 ms | 682 | 750 | S04-02's INA219 averaging |
| 100 ms | 650 | 750 | slower read |
| 750 ms | 0 | 750 | work exactly fills it |
| 900 ms | 0 | 900 | overrun — must not delay negative |

Total wake never exceeds 750 ms unless the work itself does. The old code would have given `work + 750` — 818 ms once S04-02 lands.

**On-device verification is S06-02, on a PROD-strapped unit.** Never DEV: the defect does not exist there.

## Follow-on

S02-19 also removes the single-sample VBAT read that motivated `579f935`'s dummy reads — see that task. The two dummy reads stay; they let the sampling cap settle through the 100k/100k divider, and that reasoning is still sound.
