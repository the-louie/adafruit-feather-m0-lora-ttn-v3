# S08-08 — Post-swap verification

**Estimate:** 2 h
**Backlog item:** — (cutover; see `docs/sprints/sprint-08/README.md`)
**Depends on:** S08-07
**Needs hardware:** no

## Context

The first week decides whether the swap held.

## Steps

1. Confirm the site reports continuously — no silent gaps.
2. Confirm the interval behaves: the season machine should settle within two uplinks of the cold start (it begins at Summer/index 2 and steps one level per uplink).
3. Confirm the solar signal: sun EWMA climbing from 0 over ~2–3 days at a 24 h time constant, harvest accumulating, panel voltage tracking day/night.
4. **Confirm the bonus engages.** It needs EWMA > 0.55 *and* `voltage_offset == 0`. If it never engages, the site is running at the seasonal base and something is wrong — likely the EWMA never reaching the band.
5. Watch for the first natural reboot: f_cnt resetting is the signal, and the 252-in-slot-0 alarm should now stay quiet (the fix is in).

## Done when

- [ ] Site reporting continuously for a week.
- [ ] Season settled; interval sensible.
- [ ] Solar signal behaving; bonus engaged or its absence explained.
