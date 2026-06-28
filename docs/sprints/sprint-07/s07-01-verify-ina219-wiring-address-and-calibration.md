# S07-01 — Verify INA219 wiring address and calibration

**Estimate:** 2 h
**Backlog item:** TODO #8
**Depends on:** sprint-06
**Needs hardware:** YES

## Context

The bus-voltage-at-Vin- assumption is load-bearing for the entire solar signal: it is what makes the charge-terminated case read panel Voc rather than nothing. If the breakout is wired the other way round, the policy's core input is wrong and the design does not work.

## Steps

1. Confirm the address (0x40) and that the config sanity read matches the datasheet default.
2. **Confirm bus voltage is measured load-side (Vin-)** by injecting a known voltage and checking which side it reports.
3. Confirm `setCalibration_16V_400mA()` gives the expected 0.1 mA LSB against a known current — the 32 V/2 A default would give 0.8 mA LSB, ~4% resolution against a 30 mA panel.
4. Confirm power-down draws ~15 µA and that wake-and-convert timing fits inside the Dallas conversion window.

5. **Measure li-ion's battery-voltage temperature coefficient.** Production data shows `gisebo-01`'s alkaline pack reads **+12.8 mV/°C (r = 0.93)** — a 25 °C seasonal swing implies ~321 mV of drift at constant SOC. The **solar bands are only 200 mV apart** (3.85/3.65/3.45), so if li-ion drifts anywhere near alkaline's coefficient, the solar bonus gate is measuring enclosure temperature rather than pack charge, and the whole ladder is thermal noise. Li-ion's coefficient *should* be far smaller (order 0.5 mV/°C for 1S) — confirm it rather than assume. See `docs/dev-notes/20260717-1100_battery-voltage-tracks-temperature-in-production.md`.

## Done when

- [ ] Address and calibration confirmed against a known current.
- [ ] Bus voltage confirmed load-side.
- [ ] Power-down current measured and added to the sleep budget.
- [ ] Li-ion battery-voltage temperature coefficient measured against the 200 mV solar band spacing.
