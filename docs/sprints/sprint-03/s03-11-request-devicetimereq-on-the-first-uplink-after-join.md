# S03-11 — Request DeviceTimeReq on the first uplink after join

**Estimate:** 2 h
**Backlog item:** TODO #6
**Depends on:** S03-09, S02-17
**Needs hardware:** no

## Context

With a crystal-backed RTC, one acquisition holds for months (~4 s/day drift) — so this is a one-shot, not an ongoing downlink dependency. That is what makes it affordable despite `LMIC_setLinkCheckMode(0)` deliberately quieting the stack.

## Steps

1. Confirm `LMIC_ENABLE_DeviceTimeReq` is set (S02-17).
2. Request on the first uplink after join, alongside the fast-flush.
3. It only arrives in an RX window after an uplink, and **can simply not land** — no retry storm; re-request on a later uplink if still unacquired.
4. Do not block the send path on it.

## Done when

- [ ] Request issued once per join, not per uplink.
- [ ] Failure to land is handled without blocking anything.
- [ ] Re-request policy is bounded and documented.
