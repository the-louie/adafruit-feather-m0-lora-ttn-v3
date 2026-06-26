# S03-13 — Host tests GPS to UTC conversion

**Estimate:** 1-2 h
**Backlog item:** TODO #6
**Depends on:** S03-12
**Needs hardware:** no

## Context

Pure arithmetic with a constant that can drift out of date — ideal host-test material, and one of the few things in this sprint that can be fully verified without hardware.

## Steps

1. Test against known GPS/UTC pairs.
2. Test the leap-second offset explicitly, so a future change breaks a test rather than the field.
3. Test that an implausible epoch (zero, far future) is rejected rather than accepted into the RTC.

## Done when

- [ ] Known-good pairs pass.
- [ ] Implausible values rejected.
