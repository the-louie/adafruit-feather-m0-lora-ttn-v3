# S07-06 — Quantify the harvest error bar and gate the fouling alarm

**Estimate:** 2 h
**Backlog item:** TODO #10
**Depends on:** S07-05
**Needs hardware:** YES

## Context

Two costs were accepted with the no-MPPT decision (S01-12). This task pays them.

**The harvest accumulator carries an error bar.** With the charger motorboating, harvest is measured through an averaged read of a duty-cycled waveform. The accumulator feeds the pack-health trend (S05-10) — which is the *only* replacement-planning signal available, because the supercap deliberately hides TX sag and internal resistance cannot be inferred from a 50 ms transmit. If nobody quantifies the error, noisy harvest data will eventually be mistaken for a failing pack, and someone will replace good cells.

**The fouling alarm needs gating.** Clarity (`ewma / expected_daylight_fraction`) would read low during charging periods because the bus voltage is being pulled around by the charger, not because the panel is dirty. Ungated, S05-11 false-positives every morning.

## Steps

1. From S07-05's measurements, quantify the harvest accumulator's error: magnitude and, importantly, **whether it is biased or merely noisy**. A bias that always under-reports is very different from symmetric noise when you are trending over months.
2. Document the error bar next to the accumulator in `docs/solar-variant-design.md` and at the S05-10 alarm — where the person interpreting a downward trend will actually be looking.
3. **Gate the S05-11 fouling alarm on a full pack** (`voltage_offset == 0`), where the charger has terminated and the bus reads clean panel Voc. That is the only condition under which clarity means what it claims.
4. Confirm the gating still leaves the alarm useful: with ~10× surplus the pack is full most of the summer, which is exactly when fouling matters and when the alarm still works.
5. Re-check S05-10's ability to separate pack ageing from poor harvest given the error bar. If it cannot, say so — that would mean replacement is calendar-driven, which is most of the consumable policy anyway.

## Done when

- [ ] Harvest error quantified, and bias-vs-noise established.
- [ ] Error bar documented at the accumulator and at the alarm.
- [ ] Fouling alarm gated on a full pack and confirmed still useful.
- [ ] The pack-health trend's real discriminating power stated honestly.
