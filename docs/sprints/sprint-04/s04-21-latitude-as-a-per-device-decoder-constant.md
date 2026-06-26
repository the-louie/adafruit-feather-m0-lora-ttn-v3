# S04-21 — Latitude constant and its one-binary caveat

**Estimate:** 1 h
**Backlog item:** TODO #7
**Depends on:** none
**Needs hardware:** no

## Context

Day length needs latitude. Hardcoding it **breaks the one-binary goal** that motivated the I2C probe — and it fails silently if a unit is ever deployed far from the assumed site.

The TTN capture puts the gateway at 57.807°N, 14.277°E, which is a reasonable starting point but is the *gateway*, not necessarily the sensor.

## Steps

1. Confirm the actual deployment latitude — the gateway location is not the sensor location.
2. Define the constant with a comment explaining the one-binary tradeoff and the silent-failure mode.
3. Note the escape hatch for later: the silicon-ID table proposed in `docs/generate-keys-from-feather-serial.md` would give per-board config while keeping one binary.

## Done when

- [ ] Latitude confirmed against the real site, not assumed from the gateway.
- [ ] The tradeoff and failure mode documented at the constant.
