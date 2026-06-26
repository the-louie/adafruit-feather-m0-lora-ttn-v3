# S01-15 — BOM and procurement request

**Estimate:** 1–2 h
**Backlog item:** TODO #11
**Depends on:** S01-13, S01-14
**Needs hardware:** no

## Context

**Partially superseded — 2026-07-17.** A first order is placed: **Feather M0 + DS18B20 + INA219 + panel + 18650 pack**. ETA is "later than sprint 03" and not firm.

That unblocks sprint 06 (core bench verification) and most of sprint 07 (solar bring-up). **No charger is being ordered** — see S01-12; the no-MPPT decision is made and the topology stands as designed. Still outstanding: supercapacitor and over-discharge protection, which depend on the spikes in this sprint.

This task is now about the *remainder* and the *dates*.

## Steps

1. Confirm exactly what is on the first order and get a firm ETA. Sprint 06/07 dates are provisional until this exists — and the ETA decides whether the index-2 floor gets measured before or after sprint 04 commits to it. It currently does not.
2. Order per S01-13 and S01-14 once decided:
   - **Supercapacitor** — rated above 4.2 V, since it sits on a 1S rail and standard parts are 2.7 V. A 5.5 V module is two cells in series at half the capacitance.
   - **Over-discharge protection** — protected cells or a PCM.
3. **No charger.** If someone asks, S01-12 has the reasoning.
4. Second order lands in S07-08. Record lead times; they gate the final soak and field deployment.

## Done when

- [ ] First order contents confirmed and a firm ETA recorded.
- [ ] Sprint 06/07 dates updated from provisional, or the blocker escalated.
- [ ] Supercap and protection ordered per the spikes.
- [ ] Deferred items and their gating decisions written down.
