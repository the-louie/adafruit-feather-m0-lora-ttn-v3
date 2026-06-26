# S01-09 — Backend alarm FPort 10 with battery below 4.5 V

**Estimate:** 1-2 h
**Backlog item:** TODO #10
**Depends on:** none
**Needs hardware:** no

## Context

The solar variant selects its policy by probing for the INA219 at 0x40. A dead sensor, loose wire, or hung bus makes a solar board boot the **primary-cell policy**, whose 5.0/4.3/3.5 V thresholds all sit above a full li-ion's 4.2 V — so every reading scores `voltage_offset = 3` and the unit pins itself at interval index 10 (7 days) permanently. A component fault silently decommissions the unit.

The FPort makes it observable: no healthy 6 V primary pack sits below ~4.5 V. This alarm is the only thing between a loose connector and a unit that goes quiet for a year. Build it now, before the hardware exists, so it is never 'added later'.

## Steps

1. Rule: FPort 10 or 20 with decoded `battery_v` < 4.5.
2. Severity high — a silent-decommission detector, not a warning.
3. Document the reasoning at the alarm so it survives the person who wrote it.

## Done when

- [ ] Alarm live.
- [ ] Rationale documented at the alarm, not only in the repo.
- [ ] Verified it does not fire against current fleet data (gisebo-04 at 5.233 V, gisebo-01 at 5.768 V).
