# S07-10 — Multi-day solar soak

**Estimate:** 2 h
**Backlog item:** TODO #11
**Depends on:** S07-09
**Needs hardware:** YES

## Context

Everything so far is minutes-long. The failure modes that matter here are multi-day: EWMA hunting, interval oscillation, harvest drift.

## Steps

1. Run a unit outdoors, or on a PSU driven through a simulated multi-day cycle.
2. **Confirm the EWMA does not hunt.** The time-based window was chosen specifically to prevent a sunny afternoon shortening the interval, shrinking the window to daylight-only hours, and never seeing the night. This is where that gets proven or disproven.
3. Confirm the interval settles rather than oscillating on a multi-day period.
4. Confirm the harvest accumulator grows sensibly and does not overflow or drift.

## Done when

- [ ] Multi-day run captured.
- [ ] EWMA does not hunt; interval settles.
- [ ] Harvest accumulator sane over days.
