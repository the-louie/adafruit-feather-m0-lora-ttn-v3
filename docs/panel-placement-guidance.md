# Panel placement: azimuth, tilt, and eastern obstructions

Date: 2026-07-31. Derived for the deployment latitude (Jönköping, 57.81°N) with
a high bank on the eastern shoreline, using solar geometry plus measurements
from gisebo-05 and the reference PWS (IJNKPING9). Recorded so the mounting
decision does not have to be re-derived at install time.

## The recommendation

**Face the panel south, or up to 10° west of south. Use the full 15° tilt from
vertical that the holder allows.**

Anywhere from 5° east to 15° west of south is within about 1% of optimal in the
binding season, so mechanical convenience should decide the exact bearing. The
tilt is the decision that actually matters.

## Tilt: take the 15°, it is nearly free and sometimes large

Modelled clear-sky daily yield, facing south, vertical versus 15° from vertical
(i.e. 75° from horizontal):

| season | vertical | 15° from vertical | gain |
|---|---|---|---|
| late July | 2.689 | 3.657 | **+36%** |
| equinox | 3.256 | 3.665 | **+13%** |
| winter solstice | 0.539 | 0.539 | −0.1% |

The winter neutrality is geometry, not coincidence: at 57.81°N the sun reaches
only **8.7° elevation** at winter noon, and at that elevation both a vertical
and a 75° surface see the beam within a degree or two of normal
(cos incidence ≈ 0.99 either way). So the tilt costs nothing in the season that
constrains the design and gains a third of the yield in summer.

A steep angle is the right choice for this application for two further reasons:
it is biased toward low winter sun, which is when harvest is scarce, and it
sheds snow far better than a shallow tilt. Do not be tempted toward the
"optimal annual" ~40° tilt — that optimises the season this system already
wastes (see the clamp, below).

## Azimuth: the eastern bank does not justify turning west

The intuitive argument — "the bank blocks the morning, so aim the panel at the
afternoon" — is sound in principle and almost entirely cancelled at this
latitude.

Measured obstruction at the bench position: the reference PWS stepped from
69 to 311 W/m² (UV 0 → 3) at **08:49**, when the sun stood at **29° elevation,
78° east of south**. That is a high, close bank. Astronomical sunrise was 04:57,
so it cost about **3 h 50 m** of morning sun.

Modelled yield with that bank in place, 15° tilt:

| season | best azimuth | cost of staying due south |
|---|---|---|
| **winter (Nov–Feb), the binding season** | **+10° west** | **0.5%** |
| annual | +20° west | 3.6% |
| late July | flat | ~1% |

The reason winter barely cares: at solstice the sun swings only from about −40°
to +40° azimuth across the whole short day and never climbs above 8.7°. The bank
removes the morning, but what survives is still roughly centred on south, so
rotating west gains almost nothing and begins to cost beyond +20°.

## Why the annual optimum should be ignored

The annual figure favours +20° west (+3.6%), and that number is a trap.

The system is **hard-clamped at ~116 mA** — established independently on
2026-07-30, when reference irradiance rose 21% (560 → 680 W/m²) while panel
current moved 0.0% (116.2 → 116.0 mA). The limit is the panel's Isc at this
scale or, more likely, the charger's input/charge-current ceiling. On a clear
July day the panel sits at that clamp for roughly five hours.

So **summer collection above the clamp is discarded**, and any azimuth chosen to
maximise annual yield is optimising a season that is already saturated. Winter
is where every milliamp reaches the battery, and winter says south ±10°.

The same logic caps the value of a bigger panel: at this site, harvest scales
with **hours-at-clamp**, not with peak irradiance, so a larger panel buys
nothing unless the charger is changed too.

## Uncertainty, stated

The bank's **angular width** is unmeasured and is the main sensitivity. Modelled
spanning ESE–ENE at 29°, winter prefers +10°. Modelled blocking the entire
eastern half, winter prefers +15°. Neither shifts the recommendation materially,
but if the production bank is notably wider than ESE–ENE, lean toward the
western end of the 0–10° range.

If the shoreline bank turns out **lower** than the bench's 29°, the morning
returns and due south becomes strictly better.

## A note in the deployment's favour

The bench position is a **pessimistic proxy** for the lake: 29° is a steep
obstruction, and it delays effective sunrise by nearly four hours. Whatever
energy behaviour the bench shows, the deployment should do better. That is the
safe direction for a test to be wrong in, and it means bench-derived energy
margins can be treated as lower bounds rather than estimates.
