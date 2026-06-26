# S02-05 — Change the 4-bit field to an uplink counter

**Estimate:** 1 h
**Backlog item:** TODO #2
**Depends on:** S02-04
**Needs hardware:** no

## Context

A free-running 4-bit wake counter cannot distinguish a reboot from a wraparound in principle — proven in production, where the sequence only ever takes {1, 7, 13}. Repurpose the field to something it can actually do: drop detection.

## Steps

1. Increment once per *successful* TX, in the `if (txComplete)` block of `transmitBatchAndWait()`, next to `ramCount = 0`.
2. Remove `wakeCounter`'s increment from `readAndBufferSensors()`.
3. Wire layout unchanged — still the low nibble of byte 2.

## Done when

- [ ] Counter increments once per successful TX, never on a failed one.
- [ ] Consecutive uplinks differ by exactly 1.
- [ ] Payload layout byte-identical.
