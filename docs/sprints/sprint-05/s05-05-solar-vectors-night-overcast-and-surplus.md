# S05-05 — Solar vectors night overcast and surplus

**Estimate:** 2 h
**Backlog item:** TODO #3
**Depends on:** S05-04
**Needs hardware:** no

## Context

Three more operating points covering the policy's range.

## Steps

1. **Night**: bus ~0 V, current 0, EWMA mid-range (recent sun, currently dark).
2. **Winter overcast**: low EWMA, bonus inactive, interval at the seasonal base, clarity ratio well below 1.
3. **Surplus active**: EWMA high, battery healthy, floor index 2, bonus flag set.
4. Each with the expected clarity ratio, which requires a fixed uplink timestamp in the vector.

## Done when

- [ ] Three vectors passing.
- [ ] Clarity ratios correct for their timestamps.
