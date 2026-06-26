# S04-10 — Status byte assembly

**Estimate:** 2 h
**Backlog item:** TODO #8
**Depends on:** S03-05
**Needs hardware:** no

## Context

Byte 14: bits 7–5 boot counter (wraps at 8), bits 4–0 flags — bit0 cold boot, bit1 soft reset since last uplink, bit2 clock valid, bit3 solar bonus active, bit4 last TX timed out.

The boot counter is 3 bits rather than 4 to make room for clock-valid. This byte carries reboot detection, which the primary variant loses entirely — its 9-byte payload has no room.

## Steps

1. Assemble the byte from the `.noinit` state.
2. Clear the soft-reset flag after a successful uplink, so it means 'since last uplink' rather than 'ever'.
3. Ensure the TX-timeout flag reflects the *previous* cycle — the current one has not finished yet.

## Done when

- [ ] All five flags correct and independent.
- [ ] Boot counter wraps at 8.
- [ ] Soft-reset flag clears after a successful uplink.
