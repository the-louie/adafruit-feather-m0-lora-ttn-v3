# S05-07 — Sentinel and encoder limit vectors

**Estimate:** 2 h
**Backlog item:** TODO #3
**Depends on:** S05-06, S05-20
**Needs hardware:** no

## Context

The sentinels and rails, plus the limits the encoding allows but the hardware cannot reach.

## Steps

1. Sentinel sweep: 250/251/252 interleaved with real values.
2. Rails: `0` = -10 °C and `200` = +30 °C.
3. Out-of-contract bytes 201–249 and 253–255, per the S05-20 decision.
4. Encoder limits: battery offset `0x000` (3.000 V) and `0xFFF` (7.095 V), interval index 10. **Annotate these as protocol-limit vectors that no real pack can produce** — the A7 divider saturates near 6.59 V, so 7.095 V is unreachable on hardware. A vector that cannot occur in the field should say so, or someone will chase it.

## Done when

- [ ] Sentinels, rails, and out-of-contract range covered.
- [ ] Unreachable limits annotated as such.
