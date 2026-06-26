# S04-01 — INA219 integration and calibration

**Estimate:** 2 h
**Backlog item:** TODO #8
**Depends on:** sprint-03
**Needs hardware:** no

## Context

The breakout defaults to a 32 V / 2 A calibration with a 0.8 mA LSB. Against a ~30 mA panel that is ~4% resolution and most of the dynamic range is wasted.

**Use the INA219's built-in averaging.** Without an external charger the MCP73831 is expected to motorboat — a ~30 mA panel against a 100 mA charger collapses the panel, trips UVLO, recovers, restarts. A single instantaneous reading per wake would then point-sample an oscillation, catching it mid-collapse or mid-recovery at random, and the harvest accumulator would integrate coin flips rather than a signal.

Averaging 128 samples takes ~68 ms and fits inside the 750 ms Dallas conversion window with room to spare. It is the cheapest available answer to motorboating — free, no new part, no topology change — and it may make an external charger unnecessary (see S07-06).

Whether 68 ms is long enough depends on the motorboat frequency, which nobody has measured. S07-05's scope trace answers it.

## Steps

1. Add the INA219 library; instantiate at 0x40.
2. `setCalibration_16V_400mA()` → 0.1 mA LSB.
3. **Configure 128-sample averaging** for both bus voltage and shunt current. Confirm the resulting conversion time (~68 ms) against the datasheet and budget for it in S04-02.
4. Confirm units and scaling against the datasheet, not the library's examples.
5. Note the Adafruit breakout measures bus voltage at Vin- (load side) — which is what makes the charge-terminated case read panel Voc rather than nothing. That is load-bearing for the whole signal; confirm the wiring matches (verified on hardware in S07-01).

## Done when

- [ ] INA219 reads bus voltage and current at 0.1 mA LSB.
- [ ] 128-sample averaging configured; conversion time measured and documented.
- [ ] Bus-voltage measurement point confirmed as load-side.
- [ ] Compiles.
