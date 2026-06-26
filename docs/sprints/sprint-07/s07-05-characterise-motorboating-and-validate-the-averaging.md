# S07-05 — Characterise motorboating and validate the averaging

**Estimate:** 2 h
**Backlog item:** TODO #11
**Depends on:** S07-04
**Needs hardware:** YES

## Context

**This is the measurement that could reopen the no-MPPT decision.** Everything else about that decision is settled (S01-12); this is the one empirical input.

The prediction: a ~30 mA panel against a 100 mA MCP73831 collapses the panel, trips UVLO, recovers, restarts. Estimated 30–50% harvest loss — an estimate, never measured.

The mitigation chosen instead of a charger is the INA219's **128-sample averaging (~68 ms)**, which fits inside the 750 ms Dallas window and averages across the oscillation rather than point-sampling it. **That only works if the motorboat period is comfortably shorter than 68 ms.** If it is much longer — a large input capacitor could easily make it hundreds of milliseconds — the averaging window catches a fraction of one cycle and the accumulator is still integrating coin flips.

There is no hardware fallback, by decision. If the averaging fails, the remedies are longer averaging, multiple spaced reads per wake, or accepting a wider error bar.

## Steps

1. **Scope the panel-side voltage under charge**, real panel, real sun, pack not full (motorboating only happens while actively charging — a full pack terminates and the panel goes unloaded).
2. **Measure the oscillation period.** This is the number the whole decision hinges on. Compare against the ~68 ms averaging window.
3. Measure the panel's real Voc and Vmp — a "5 V" polysilicon panel is typically Voc ~6 V, Vmp ~5 V, and both matter.
4. **Confirm the `sun_present = bus_mV > 3000` threshold survives.** During the collapsed phase the bus could sit well under 3 V, so check what an averaged read reports in full sun with a charging pack. The healthy-battery gate means this cannot corrupt the interval decision — but it can corrupt the fouling alarm (S07-06).
5. Measure actual harvest against the estimate; update `docs/solar-variant-design.md`.
6. **If the averaging is insufficient**, do not reach for a charger — raise it, and pick from longer averaging / multiple reads / wider error bar.

## Done when

- [ ] Motorboat period measured and compared against the 68 ms window.
- [ ] Real Voc/Vmp measured.
- [ ] The day/night threshold confirmed to survive an averaged read while charging.
- [ ] Real harvest measured; design doc updated.
- [ ] If the averaging fails, the alternative is chosen and the no-MPPT decision explicitly re-affirmed or reopened.
