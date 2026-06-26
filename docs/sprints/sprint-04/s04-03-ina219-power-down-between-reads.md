# S04-03 — INA219 power-down between reads

**Estimate:** 1 h
**Backlog item:** TODO #8
**Depends on:** S04-02
**Needs hardware:** no

## Context

~15 µA in power-down versus ~1 mA active. It joins a sleep budget already dominated by ~290 µA of board quiescent — so this is not where the wins are, but leaving it running is free waste.

## Steps

1. Put the INA219 into power-down mode after each read.
2. Confirm it wakes reliably — a power-down that needs a settling time before the next conversion is a trap.
3. Add its 15 µA to the documented sleep budget alongside the supercap leakage from S01-13.

## Done when

- [ ] Powered down between reads.
- [ ] Wake-and-convert timing verified against the datasheet.
- [ ] Sleep budget updated.
