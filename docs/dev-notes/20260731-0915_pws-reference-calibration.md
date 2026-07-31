# A horizontal irradiance reference settles three open questions

Date: 2026-07-31 09:15 CEST
Source: Wunderground PWS **IJNKPING9** (Norrängen), 5-minute solar radiation in
W/m², horizontally mounted, **behind the same eastern hill as gisebo-05** — so
it shares the site's terrain obstruction and differs only in orientation. The
operator supplied it; it is the first independent instrument this project has
had against the panel.

## 1. The effective horizon: the hill delays sunrise by nearly four hours

The reference shows an unmistakable step as the sun crests the hill:

```
07-31   08:44   69.1 W/m²  UV 0
        08:49  310.9 W/m²  UV 3      <- 4.5x in five minutes
07-30   08:54  327.9 W/m²  UV 3      <- same transition, same time
```

Astronomical sunrise is **04:57**; effective sunrise at this position is
**~08:47**. The hill costs about **3 h 50 m** of morning sun. Sunset is
unaffected (the hill is east), so the site's true solar window is roughly
08:47–21:08 = 12.3 h, against an astronomical 16.2 h.

This is a property of the deployment site, not the device, and it is worth
carrying into any energy-budget estimate for this location. A lake installation
with a clear eastern horizon would harvest substantially more than this bench
position, so bench numbers here are **conservative**, which is the safe
direction.

## 2. The weak morning was half weather, half orientation

The question raised by the balcony move: was the poor morning caused by cloud or
by the panel now pointing west of south? The reference separates them, because
it measures the light that arrived regardless of where the panel points.

| hour | windowsill 07-30 | ref | mA/(W/m²) | balcony 07-31 | ref | mA/(W/m²) |
|---|---|---|---|---|---|---|
| 06:3x | 4.9 mA | 40 | 0.123 | 1.4 mA | 28 | 0.050 |
| 07:3x | 7.4 mA | 79 | 0.094 | 2.5 mA | 46 | 0.054 |
| 08:3x | 11.8 mA | 100 | 0.118 | 3.1 mA | 55 | 0.056 |

- **Irradiance was ~1.8× higher** on the 30th (genuinely brighter morning).
- **The windowsill converted ~2.1× better** per unit horizontal irradiance.
- Product ≈ 3.8×, which is exactly the observed ratio.

So **both explanations were right, in roughly equal measure.** Yesterday was a
brighter morning *and* the balcony position is worse for morning light. The
earlier note (`d015c87`) leaned toward orientation alone; this halves that
claim. If a south aspect is available, it is still the better placement, but
the expected gain is about 2×, not 4×.

## 3. The 116 mA ceiling is a hard clamp, proven independently

Yesterday's conclusion rested on the panel's own repetition. The reference makes
it decisive, because irradiance varied while current did not:

| time | device | reference |
|---|---|---|
| 11:36 | 116.2 mA | 560 W/m² |
| 12:36 | 115.9 mA | 620 W/m² |
| 13:37 | 116.0 mA | 680 W/m² |

**Irradiance rose 21%; current moved 0.0%.** That is a clamp — the panel's Isc
at this scale, or more likely the charger's input/charge-current limit. Two
consequences:

- Extra irradiance buys **plateau width, not height**, so harvest at this site
  scales with hours-above-clamp. Panel sizing beyond what already reaches the
  clamp buys nothing without changing the charger.
- The clamp caps daily harvest at roughly `116 mA × hours-at-clamp`. On the 30th
  that was ~5 h, giving the ~580 mAh of afternoon harvest actually observed.

## 4. A limitation of `clarity` this reference exposes

`clarity = sun_ewma / expectedDaylightFraction` was intended (item 7) to
separate weather and panel fouling from season. The reference shows it cannot
separate **weather**, and the reason is the predicate's sensitivity:

At the balcony's measured 0.056 mA/(W/m²), the two-arm predicate's **1 mA
current floor trips at roughly 18 W/m²** — which the reference reached at
**05:44 today, three hours before the sun cleared the hill**. Pure diffuse
skylight satisfies `sunPresent()`. An overcast midday (100–200 W/m²) would give
6–11 mA, far above the floor.

So `sun_ewma` measures *"is there any usable light"*, not *"is there good sun"*,
and `clarity` therefore tracks **panel obstruction** (snow, leaves, shade, a
failed sensor) rather than weather. That is still the more valuable of the two —
it is the fault a bare EWMA hides, and the reason item 7 exists — but the
"separates weather" half of the original description is not supported by
measurement. Worth correcting expectations rather than discovering it later from
a clarity figure that never moves in bad weather.

Note this does **not** mean the 1 mA floor is wrong. It was validated on
2026-07-30 by catching dawn at exactly 1.1 mA when the relative arm was still
failing at −167 mV, and a higher floor would have delayed recovery by an hour.
The floor is correct for its job (harvest detection); it simply makes clarity a
different instrument than intended.

## Follow-up worth having

The PWS is a standing calibration source. Two uses:

1. **Item 15's instantaneous-channel check** can now be done partly without a
   DMM: panel current against reference irradiance over a clear day gives the
   response curve, and any drift in that relationship over months is a panel or
   charger fault.
2. **Item 10's battery-trend metric** gains a covariate. A falling trend during
   a week of high reference irradiance means something is wrong with the device;
   the same trend during a dull week is just weather. That is a materially
   better alarm than the trend alone.
