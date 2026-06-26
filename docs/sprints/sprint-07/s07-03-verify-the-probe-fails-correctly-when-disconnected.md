# S07-03 — Verify the probe fails correctly when disconnected

**Estimate:** 2 h
**Backlog item:** TODO #4
**Depends on:** S07-02
**Needs hardware:** YES

## Context

**The most important test in this sprint.** A dead INA219 makes a solar board boot the primary-cell policy, whose 5.0/4.3/3.5 V bands all sit above a full li-ion's 4.2 V — so every reading scores `voltage_offset = 3` and the unit pins itself at a 7-day interval, permanently and silently.

This path has never run on hardware. It is the difference between a loose connector being a minor fault and a unit going quiet for a year.

## Steps

1. Disconnect the INA219 and boot. Confirm the unit does not hang.
2. Confirm whatever S03-02 decided actually happens — the sanity check fires, or the primary policy is selected and the backend alarm catches it.
3. **Confirm the S01-09 alarm (FPort 10 + battery < 4.5 V) fires end to end.** An alarm nobody has watched fire is not an alarm.
4. Test a *hung* bus (SDA held low), not just an absent device. They are different failures and only one is covered by an address probe.

## Done when

- [ ] Disconnected INA219 does not hang boot.
- [ ] The S03-02 behaviour happens as designed.
- [ ] The S01-09 alarm fires end to end.
- [ ] A hung bus is handled distinctly from an absent device.
