# Battery voltage tracks temperature in production — the cold-battery illusion, measured

## Summary

The design's most subtle piece of reasoning — that a pack reads low in the cold and therefore voltage must not drive the season — is **confirmed from production telemetry**, with a magnitude.

`gisebo-01`'s reported battery voltage correlates with temperature at **r = +0.93, slope +12.8 mV/°C** across 20 uplinks. Its battery appears to *rise* +19.3 mV/day over 2.4 days. A primary cell does not recharge; it is warming, not charging.

## The data

Full retained TTN window, 2026-07-15 → 07-17.

| | gisebo-01 | gisebo-04 |
|---|---|---|
| deployment | lake, production | **fridge, cold test of primary lithium** |
| interval | 30 min (byte 0 = index 4) | 5 min, fixed (V5) |
| span | 2.34 d, 20 uplinks | 2.38 d, 119 uplinks |
| battery | 5768 → 5813 mV (**+19.3 mV/day**) | 5233 → 5227 mV (**−2.5 mV/day**) |
| temp range | 16.9 – 20.8 °C | 7.7 – 10.2 °C |
| **battery vs temp** | **r = +0.93, +12.8 mV/°C** | r = −0.24, −1.6 mV/°C |

## The telemetry distinguishes the two chemistries

This is the interesting part. The pack was described as "4×AA, or 2× 3 V lithium" without specifying which unit had which. **The temperature coefficients tell them apart:**

- **gisebo-01: +12.8 mV/°C.** Alkaline cells have an open-circuit temperature coefficient around +1–3 mV/°C each; four in series gives ~4–12 mV/°C. **gisebo-01 is almost certainly 4×AA alkaline.**
- **gisebo-04: −1.6 mV/°C, r = −0.24** — no meaningful correlation. Lithium primaries have a famously flat temperature response. **Consistent with 2× 3 V lithium**, which is exactly what it is being tested with.

That also independently corroborates the discharge-curve claim already in `CLAUDE.md`: alkaline slopes, lithium sits flat. gisebo-04 is drifting **−2.5 mV/day at a steady 9 °C** — the flat curve, in the cold, in real time.

Caveat on gisebo-04's correlation: a fridge holds temperature, so its 2.5 °C range gives almost nothing to correlate against. Its r is weak because the *experiment* is well controlled, not because the coefficient is proven absent.

## What it means

**The cold-battery illusion is real and now has a number.** Extrapolating +12.8 mV/°C across a 25 °C seasonal swing implies **~321 mV of temperature-driven drift** in the reported voltage — a pack at constant true SOC reading a third of a volt lower in winter than summer.

Against the primary bands (5.00 / 4.30 / 3.50 V, **700 mV apart**) that is ~46% of a band: enough to shift the interval a step for purely thermal reasons, not enough to cross a band outright.

**This validates the design's central mitigation.** master-plan says season is driven by water temperature precisely so "a temporarily low voltage in cold weather does not permanently lock a long interval". That reasoning was an argument; it is now measured. Had voltage been allowed to drive the season, this drift would have fed straight into it.

**It does not threaten the solar bands, but that needs confirming.** The solar bands are only **200 mV apart** (3.85 / 3.65 / 3.45), so 321 mV of drift would span more than a whole band — alarming, except the coefficient measured here is alkaline's. Li-ion's OCV temperature coefficient is much smaller (order 0.5 mV/°C for 1S). **gisebo-05 should be checked for this specifically**: if li-ion drift approaches the 200 mV band spacing, the solar bonus gate is measuring the enclosure's temperature rather than the pack's charge.

## Extrapolation warning

n = 20, across only 4 °C of range, over 2.3 days. The 25 °C figure is an extrapolation of 6×. The correlation is strong and the mechanism is physically expected, but the coefficient should be re-measured across a real seasonal swing before anyone designs against the exact number.

The confound worth naming: the DS18B20 is in the **water**, the battery is in **air** inside the enclosure. Air temperature drives both, so the correlation is indirect — and water lags air. That the correlation survives that lag at r = 0.93 makes it more convincing, not less.

## Do not disturb gisebo-04

It is running a cold-weather test of primary lithium cells and is the only source of data on that chemistry. Reflashing it would give the project an on-device test unit — and destroy a months-long experiment that is currently answering, for free, the discharge-curve question the plan could otherwise only guess at.

Its accelerated duty cycle (288 wakes/day, 6× production) makes it more valuable still: over months it bears directly on the per-wake versus quiescent question that TODO #11 flags as a 35× disagreement.
