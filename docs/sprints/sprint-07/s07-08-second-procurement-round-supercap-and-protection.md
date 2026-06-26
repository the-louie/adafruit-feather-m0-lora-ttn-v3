# S07-08 — Second procurement round: supercap and protection

**Estimate:** 1 h
**Backlog item:** TODO #11
**Depends on:** S01-13, S01-14
**Needs hardware:** no

## Context

The first order covered Feather, DS18B20, INA219, panel and pack. **No charger is needed** — the no-MPPT decision (S01-12) keeps the onboard MCP73831 and the topology as designed.

Outstanding: the supercapacitor and over-discharge protection, both of which depended on spikes rather than on the charger question.

## Steps

1. Order the supercap per S01-13 — **rated above 4.2 V**, since it sits on a 1S rail and standard parts are 2.7 V. A 5.5 V module is two cells in series at half the capacitance, so size accordingly.
2. Order protected cells or a PCM per S01-14. Confirm the cutoff sits **below** `VOLTAGE_CRITICAL_V` (3.45 V) so firmware always acts first, and above the cell's damage threshold.
3. Add both leakage figures to the documented sleep budget — they join the 15 µA INA219 against a budget already dominated by ~290 µA of board quiescent.
4. Record lead times; they gate the final soak (S07-10) and field deployment.

## Done when

- [ ] Supercap and protection ordered.
- [ ] Protection cutoff confirmed between brownout and cell damage.
- [ ] Leakage figures added to the sleep budget.
- [ ] Lead times recorded.
