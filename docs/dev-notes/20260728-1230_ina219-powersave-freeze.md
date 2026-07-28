# INA219 froze after the first read of each boot — powerSave(true) was never undone

Date: 2026-07-28 12:30 CEST
Fixes the defect documented in `20260728-1100_overnight-gisebo-05-tx-timeouts-and-frozen-ina219.md` §3.

## Defect

`readAndBufferSensors()` ended every solar read with `ina219.powerSave(true)`
(S04-03, ~15 µA between reads) — and nothing ever called `powerSave(false)`.
`powerSave(true)` writes `MODE = POWERDOWN` into the config register, which
stops conversions entirely; reads still ACK and return the conversion
registers' last pre-powerdown contents. So each boot got exactly one honest
sample and then repeated it forever.

Overnight evidence (2026-07-27/28): `panel_v`/`panel_ma` byte-identical at
3.852 V / 15.9 mA from dusk through sunrise; `harvest_mah` accumulated 192 mAh
of charge that never flowed (+16 mAh per cycle = frozen 15.9 mA × 3600 s);
`sun_ewma` climbed 0.004 → 0.396 in the dark (frozen 3852 mV > the 3000 mV
`SUN_PRESENT_MV` gate) and was ~7 cycles from latching the interval bonus
permanently — held off only by the second gate (`voltage_offset == 0` needs
≥ 3.85 V; the pack sat at 3.72 V).

## Fix

In the solar read path:

1. **`ina219.powerSave(false)` before the reads**, then a 5 ms
   `os_runloop_once()` wait (first 12-bit shunt+bus conversion after wake is
   ~1.1 ms; the wait sits inside the DS18B20 750 ms window measured from
   `convStart`, so the wake adds nothing to the cycle). `powerSave(true)`
   stays at the end — the 15 µA saving was never the problem.

2. **`g_ina219ReadOk` now keys on the I2C transaction, not a voltage floor.**
   The old check (`busMv >= 500`) was only ever satisfied by the frozen value:
   a *live* dark panel legitimately reads ~0 mV all night — that is the sun
   signal working — so the first night after this fix would have raised
   `DIAG_FAULT_INA219_READ_FAIL` nightly. An absent or hung part NAKs, which
   `Adafruit_INA219::success()` reports (`getBusVoltage_raw` stores the
   BusIO read result); a healthy part ACKs whatever the light level. The
   `busMv < 20000` top clamp stays to catch an ACKing-but-wedged bus.

Also corrected the stale "~68 ms of averaging" comments: the configured
`setCalibration_16V_400mA` mode is 12-bit single-sample (~532 µs per
conversion); the INA219 borrow from the conversion window is ~7 ms.

## Self-healing and what to expect after flashing

- The EWMA poison decays on its own time constant once real night readings
  arrive; a re-flash power-cycles the board anyway (`.noinit` does not survive
  it), so `sun_ewma`/`harvest_mah` restart from zero regardless.
- **Falsifiable check**: after flashing in daylight, `panel_v`/`panel_ma`
  must differ from 3.852 V / 15.9 mA and move frame to frame; after sunset
  `panel_v` must collapse and `sun_ewma` must decay; `harvest_mah` must stop
  advancing at night. TODO item 15 (bench-verify metering against a meter)
  remains the quantitative follow-up.

## Verification

Compiles (73548 B, 28%); host + decoder suites green (no wire change; the
probe/decision logic in `variant_probe.h` is untouched — the probe's own
soft-reset wakes the part at boot, so probe and first-boot reads were never
affected).
