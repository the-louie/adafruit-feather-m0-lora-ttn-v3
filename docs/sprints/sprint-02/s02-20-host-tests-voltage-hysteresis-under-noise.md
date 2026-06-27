# S02-20 - Host tests: voltage hysteresis under noise

**Estimate:** 2 h
**Backlog item:** TODO #8
**Depends on:** S02-19
**Needs hardware:** no

## Context

**A single-point test cannot see this defect.** Feeding clean voltages across a band edge shows a tidy transition either way. The bug only appears when the input *dithers* - which is exactly what +/-19 mV of ADC noise does to a pack sitting on an edge.

Test the noise, not the steps.

## Steps

1. **The test that matters:** hold the pack at exactly a band edge (5.00 V primary, 3.85 V solar), add +/-19 mV of simulated noise, run 200 wakes, assert `voltage_offset` **never changes**. Run it against pre-hysteresis code first and watch it fail - otherwise it is not testing what it claims.
2. Test the asymmetry: falling through 5.00 V degrades immediately; rising back requires 5.05 V, not 5.00 V.
3. Test multi-step drops - a pack collapsing from healthy to below critical must reach offset 3 in one call, not one band per wake. Unlike the season machine, the voltage ladder must **not** be a stepping machine: a failing pack should not need three wakes to be recognised.
4. Test that averaging reduces spread: 16 samples of +/-19 mV noise should yield ~+/-5 mV.
5. Replay the real case that matters: **3.85 V on the solar bands** (gisebo-05's bonus gate) with noise -> `voltage_offset` stays 0, bonus stays latched. Also 5.768 V on the primary bands (gisebo-01 today) -> offset 0, trivially stable since it sits 0.77 V clear of the edge.

## Done when

- [ ] The dithering test fails pre-fix and passes post-fix.
- [ ] Asymmetric recovery pinned.
- [ ] Multi-step degradation works in one call.
- [ ] The gisebo-04 production value is a stable test case.
