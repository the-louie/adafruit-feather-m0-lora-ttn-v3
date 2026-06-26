# S06-01 — Bench bring-up of a PROD-strapped unit

**Estimate:** 2 h
**Backlog item:** TODO #12
**Depends on:** hardware
**Needs hardware:** YES

## Context

**PROD-strapped, not DEV.** Pin 11 floating. Every bug this sprint hunts is PROD-only by construction — a DEV unit would show none of them.

PROD detaches USB, so there are no serial logs. Telemetry comes over the air on FPort 10.

## Steps

1. Assemble a unit, strap floating for PROD, join TTN.
2. Confirm uplinks arrive on FPort 10.
3. Accept that debugging is via LoRa. That is the deal PROD makes.
4. If a debug UART on spare pins is feasible, it would pay for itself immediately — consider it.

## Done when

- [ ] PROD unit joins and uplinks.
- [ ] A telemetry-only debug loop is workable.
