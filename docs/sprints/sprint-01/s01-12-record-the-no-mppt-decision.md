# S01-12 — Record the no-MPPT decision

**Estimate:** 1 h
**Backlog item:** TODO #11
**Depends on:** none
**Needs hardware:** no

## Context

**No MPPT — decided 2026-07-17.** No external charger; the panel feeds the Feather's USB pin through a Schottky and the INA219, charging via the onboard MCP73831. The topology stands as originally designed.

This was a spike. It is now a decision, so the task is to write the rationale down before it evaporates — the reasoning is the perishable part, and someone will look at a motorboating panel in six months and wonder why nobody fixed it.

The MCP73831 is set to 100 mA against a ~30 mA panel, so it will drag the panel off its knee until it browns out, releases, recovers, restarts — costing perhaps 30–50% of harvest. Accepted because:

- **No energy case.** ~10× claimed summer surplus and 500 days of dark-spell reserve; halved harvest still leaves ~5×.
- **The healthy-battery gate already covers the signal risk.** Motorboating only occurs while actively charging; a full pack terminates the charger and the bus sits cleanly at Voc. The solar bonus is gated on `voltage_offset == 0` — a near-full pack — so the corrupted EWMA and the decision that consumes it never overlap.
- **The INA219's 128-sample averaging (~68 ms) is a free mitigation**, fitting inside the 750 ms Dallas window. It averages across the oscillation rather than point-sampling it.
- **An external charger is not a drop-in**: it means bypassing the onboard charger into BAT, plus two chargers on one pack whenever USB is plugged in DEV.

## Steps

1. Write the decision and its rationale into a dev-note. `docs/solar-variant-design.md` already carries it — the dev-note is the dated record of *when and why*.
2. Record the accepted costs explicitly: the harvest accumulator carries an error bar (quantified in S07-06), the fouling alarm must gate on a full pack, and **there is no hardware fallback** if the averaging proves insufficient.
3. Record what would reopen it: S07-05 measuring a motorboat period much longer than the ~68 ms averaging window. Remedies then are longer averaging, multiple reads per wake, or a wider error bar — not a charger.
4. Record the rejected options so nobody re-runs this: bq24074 (no amazon.se listing; an Electrokit/Digikey/Mouser part), CN3065 (right chip, but the input-regulation loop was never confirmed from the Consonance datasheet), CN3791 (**the amazon.se listings are 12 V variants that would hold a 5 V panel at a setpoint it can never reach and never charge at all**).

## Done when

- [ ] Dev-note committed with the decision, the date, and the reasoning.
- [ ] Accepted costs recorded, including the absence of a hardware fallback.
- [ ] The reopening condition named, so S07-05 knows it is the deciding measurement.
- [ ] Rejected options recorded, especially the CN3791 12 V trap.
