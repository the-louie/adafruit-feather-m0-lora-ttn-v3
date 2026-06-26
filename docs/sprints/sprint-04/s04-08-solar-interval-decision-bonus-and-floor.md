# S04-08 — Solar interval decision bonus and floor

**Estimate:** 2 h
**Backlog item:** TODO #8
**Depends on:** S04-07, S04-04
**Needs hardware:** no

## Context

`season_base`, then **only if `voltage_offset == 0` AND the solar bonus is latched on** (EWMA > 0.55 engages, < 0.45 releases), subtract a **fixed 2 steps**. Clamp [2, 10]. Decided 2026-07-17 — they were undefined through the whole design phase and are the two constants this task exists to implement.

| season | base | with bonus |
|---|---|---|
| Summer | 4 (30 min) | **2 (5 min)** — the floor |
| Fall/Spring | 5 (60 min) | 3 (15 min) |
| Winter | 7 (6 h) | 5 (60 min) |

**Threshold: engage at EWMA > 0.55, disengage below 0.45** — a hysteresis band centred on the chosen 0.5, not a bare threshold.

A bare 0.5 **flaps**. The EWMA has a 24 h time constant fed a 24 h day/night cycle, so it ripples ±0.11 around its mean every single day. Steady-state swings:

| season | daylight | EWMA daily swing | vs 0.55/0.45 |
|---|---|---|---|
| Summer (clear) | 15.6 h | 0.533 → 0.756 | never below 0.45 → **stays ON** |
| Fall/Spring | 9.6 h | 0.286 → **0.522** | never reaches 0.55 → **stays OFF** |
| Winter | 6.0 h | 0.165 → 0.350 | → **stays OFF** |

Fall/Spring peaks at 0.522 *every afternoon* — so a bare 0.5 would engage the bonus daily and drop it nightly, dithering the interval 5↔3 through both shoulder seasons. The band separates the three regimes with no crossing at all, which is precisely the "summer only in practice" behaviour intended.

This mirrors the season machine's 1 °C hysteresis, for the same reason and against the same failure.

**The band is provisional**: no field EWMA distribution exists, which is why byte 11 uplinks the raw value. The *shape* (a band, not a threshold) is not provisional — a bare threshold is a known defect here.

The healthy-battery gate is what makes this safe: sun never shortens the interval on a pack that is already struggling, so the two signals cannot fight. And the loop self-corrects — if shortening outruns harvest, the pack drains, the bonus disappears, and the interval returns to the seasonal baseline. That self-correction is the only thing making the unmeasured index-2 floor defensible.

## Steps

1. Implement the ladder with the healthy-battery gate.
2. Floor at index 2 (5 min), batch stays 6 → ~48 uplinks/day, inside TTN's 30 s/day fair use.
3. Keep the two write points for `currentIntervalIndex`: setup, and post-`EV_TXCOMPLETE`. Byte 0 must keep meaning 'the interval these six samples were taken at'.
4. Set the bonus-active flag for the status byte.

## Done when

- [ ] Bonus applies only at `voltage_offset == 0`.
- [ ] Clamped to [2, 10].
- [ ] The two-write-point invariant preserved.
