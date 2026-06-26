# S04-11 — Solar payload append

**Estimate:** 2 h
**Backlog item:** TODO #8
**Depends on:** S04-10, S02-15
**Needs hardware:** no

## Context

Six bytes appended to the shared bytes 0–8: panel V (30 mV/LSB), panel I (0.5 mA/LSB), sun EWMA (0–255 = 0.0–1.0), harvest (16-bit, 1 mAh/LSB), status.

15 bytes total — trivially inside DR0's 51. The size was chosen deliberately: with no bench testing, **the payload is the only instrument**, so the policy's own inputs must be auditable from the backend.

## Steps

1. Append after byte 8 via `appendPayload`.
2. **Bytes 0–8 must stay byte-identical to the primary variant.** Assert it in a test, do not assume it.
3. Clamp each field at its encoding limits rather than wrapping.

## Done when

- [ ] 15-byte payload assembled correctly.
- [ ] Bytes 0–8 byte-identical, proven by test.
- [ ] Fields clamp rather than wrap.
