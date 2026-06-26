# S06-07 — Verify DeviceTimeReq lands through a real gateway

**Estimate:** 2 h
**Backlog item:** TODO #6
**Depends on:** S06-01
**Needs hardware:** YES

## Context

It only arrives in an RX window after an uplink, and can simply not land. The gateway at 57.807°N is a real one with real duty-cycle constraints — this is exactly the kind of thing that works in theory and not at a lake.

## Steps

1. Confirm the request is sent on the first uplink after join.
2. Confirm the response arrives and the RTC is set.
3. Verify GPS→UTC conversion against known-good time — an 18-second error would be invisible and would quietly skew every clarity ratio.
4. Test the degraded path: block the downlink, confirm the unit runs on the raw EWMA and reports clock-invalid.

## Done when

- [ ] Time acquired through a real gateway.
- [ ] Conversion correct to the second.
- [ ] Degraded path confirmed working and visible.
