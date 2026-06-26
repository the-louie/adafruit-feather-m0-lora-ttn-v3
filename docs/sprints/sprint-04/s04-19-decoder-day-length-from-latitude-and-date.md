# S04-19 — Day length calculation from latitude and date

**Estimate:** 2 h
**Backlog item:** TODO #7
**Depends on:** S03-12
**Needs hardware:** no

## Context

Day length normalises the sun-presence EWMA: `clarity = ewma / expected_daylight_fraction`. ~1.0 means clear skies; persistently low means overcast — **or snow, leaves, or shade on the panel**, a fault class a vertical mount in July would otherwise hide behind a plausible-looking low EWMA.

Note this may be a **backend-only** calculation: the backend has the uplink timestamp and the latitude constant, so it can compute the expectation itself at zero payload cost. Implement in firmware only if the policy keys on clarity directly.

## Steps

1. Decide first: does the *firmware* need day length, or only the backend? Prefer the backend — it is free there.
2. If firmware: solar declination + hour angle, ~20 lines. Watch float cost on a SAMD21 with no FPU (fine at one call per wake).
3. Use the fixed latitude constant (S03-17).

## Done when

- [ ] The firmware-vs-backend decision made and recorded.
- [ ] If implemented, correct against reference values at the deployment latitude.
