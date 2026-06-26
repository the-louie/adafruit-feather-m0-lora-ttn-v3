# S01-14 — Spike over-discharge protection

**Estimate:** 1-2 h
**Backlog item:** TODO #11
**Depends on:** none
**Needs hardware:** no

## Context

The Feather browns out near 3.4 V but keeps leaking tens of µA afterward, walking a li-ion cell toward destruction over a long dark spell. Firmware cannot help once it is off — `VOLTAGE_CRITICAL_V` at 3.45 V protects the *data*, not the *cell*.

The 500-day dark-spell figure assumes the system is running. This task covers the case where it is not.

## Steps

1. Decide: protected cells (integrated PCM) versus a pack-level protection board.
2. Check the PCM's own quiescent draw — it joins the sleep budget too.
3. Confirm the cutoff sits below `VOLTAGE_CRITICAL_V` (3.45 V) so firmware always acts first, and above the cell's damage threshold.

## Done when

- [ ] Protection approach decided.
- [ ] Cutoff confirmed to sit between brownout and cell damage.
- [ ] PCM quiescent added to the sleep budget.
