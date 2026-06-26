# S03-02 — Probe misdetect safety and fallback

**Estimate:** 1-2 h
**Backlog item:** TODO #4
**Depends on:** S03-01
**Needs hardware:** no

## Context

**The probe conflates 'has an INA219' with 'is a li-ion pack.'** A dead sensor, loose wire, or hung bus makes a solar board boot the primary-cell policy — whose 5.0/4.3/3.5 V bands all sit above a full li-ion's 4.2 V. Every reading then scores `voltage_offset = 3` and the unit pins itself at interval index 10 (7 days) permanently. A component fault silently decommissions the unit for a year.

The backend alarm (S01-09) catches it, but only after the fact. This task decides whether firmware should also refuse.

## Steps

1. Add a sanity check: if the probe says 'primary' but VBAT reads below ~4.5 V, the combination is impossible for a healthy 6 V pack.
2. Decide the response — options, with the tradeoff stated in the dev-note:
   - Trust the probe, rely on the backend alarm (silent but simple).
   - Refuse to apply primary bands and run a safe default interval, making the fault loud.
3. Whichever is chosen, the FPort still reveals the selected policy, so the backend can always tell.

## Done when

- [ ] Sanity check implemented or explicitly declined with a written reason.
- [ ] The decision is recorded in the dev-note, not just the code.
