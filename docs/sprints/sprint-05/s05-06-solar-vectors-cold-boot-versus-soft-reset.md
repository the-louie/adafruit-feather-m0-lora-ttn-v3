# S05-06 — Solar vectors cold boot versus soft reset

**Estimate:** 1-2 h
**Backlog item:** TODO #3
**Depends on:** S05-05
**Needs hardware:** no

## Context

Reboot detection exists only on the solar variant now, via the status byte. It is also the thing that replaces the field that never worked — worth testing properly.

## Steps

1. **Cold boot**: cold-boot flag set, boot counter at its initial value, harvest accumulator zeroed.
2. **Soft reset**: soft-reset flag set, cold-boot clear, state preserved, harvest continuing.
3. **Boot counter wrap** at 8 (3 bits).
4. **Clock invalid**: clarity must decode as `null`, not as a number.

## Done when

- [ ] Cold boot and soft reset are distinguishable.
- [ ] Counter wrap covered.
- [ ] Clock-invalid produces null clarity.
