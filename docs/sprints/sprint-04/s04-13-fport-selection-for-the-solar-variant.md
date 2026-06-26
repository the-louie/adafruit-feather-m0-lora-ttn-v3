# S04-13 — FPort selection for the solar variant

**Estimate:** 1 h
**Backlog item:** TODO #8
**Depends on:** S04-07
**Needs hardware:** no

## Context

FPorts: 10 primary/PROD, 20 primary/DEV, 11 solar/PROD, 21 solar/DEV. The variant is carried by the FPort, so no policy-ID byte is needed — and it is what makes the probe-misdetect alarm (S01-09) possible at all.

## Steps

1. Return the FPort from the policy based on `runMode`.
2. Confirm the gateway and TTN application are not filtering 11/21.
3. Confirm the backend routes them.

## Done when

- [ ] Correct FPort per variant and mode.
- [ ] 11/21 confirmed unfiltered end to end.
