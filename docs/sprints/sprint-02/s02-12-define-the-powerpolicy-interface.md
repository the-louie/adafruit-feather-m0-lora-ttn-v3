# S02-12 — Define the PowerPolicy interface

**Estimate:** 2 h
**Backlog item:** TODO #4
**Depends on:** S02-10
**Needs hardware:** no

## Context

Two policies behind one interface, selected at runtime by an I2C probe. The vtable cost is irrelevant on SAMD21. See `docs/solar-variant-design.md` § Architecture.

## Steps

1. `power_policy.h`: `begin()`, `onWake()`, `decideInterval(tempC, vbat)`, `appendPayload(buf)`, `payloadLen()`, `fport(runMode)`.
2. Keep it narrow — if the solar policy needs something the primary does not, that is a sign the boundary is wrong.
3. The season machine sits **outside** the interface: both policies consume it, neither owns it.

## Done when

- [ ] Interface defined and documented.
- [ ] Both policies plausibly implementable behind it — sketch out the solar one on paper before committing.
