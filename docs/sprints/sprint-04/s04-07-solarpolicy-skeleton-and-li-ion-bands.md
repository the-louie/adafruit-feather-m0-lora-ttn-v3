# S04-07 — SolarPolicy skeleton and li-ion bands

**Estimate:** 2 h
**Backlog item:** TODO #8
**Depends on:** S03-01, S02-12
**Needs hardware:** no

## Context

Bands: **3.85 / 3.65 / 3.45 V**, replacing the primary's 5.0/4.3/3.5. Healthy at 3.85 is roughly 60%+ SOC, where a solar-fed pack should normally live, so the bonus is usually available. Critical at 3.45 keeps margin above the Feather's ~3.4 V brownout.

VBAT is sampled at wake and before `LMIC_setTxData2()`, so it reads essentially open-circuit — cold sag under the 120 mA TX load never enters the measurement, and the supercap covers it anyway.

## Steps

1. Implement `PowerPolicy` for solar.
2. Li-ion bands as constants with a comment explaining the SOC reasoning — the numbers look arbitrary otherwise.
3. **Reuse S02-19's `voltageOffset()` with hysteresis** — do not re-derive it. The bands differ per variant; the dithering mitigation does not. The 3.85 V edge gates the solar bonus, so a bare threshold here thrashes the interval 2↔4 (5 min ↔ 30 min) every wake on ADC noise alone — the worst instance of this defect anywhere in the design.
3. Reuse the shared season machine unchanged.
4. Note li-ion's flat 3.6–3.9 V plateau makes voltage a poor SOC proxy; the bands are placed to work with that, not against it.

## Done when

- [ ] SolarPolicy implements the interface.
- [ ] Bands documented with reasoning.
- [ ] Season machine reused, not duplicated.
