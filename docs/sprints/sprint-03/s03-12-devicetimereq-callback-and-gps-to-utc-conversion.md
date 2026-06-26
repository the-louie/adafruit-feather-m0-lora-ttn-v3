# S03-12 — DeviceTimeReq callback and GPS to UTC conversion

**Estimate:** 2 h
**Backlog item:** TODO #6
**Depends on:** S03-11
**Needs hardware:** no

## Context

The network returns **GPS epoch**, not UTC. Conversion needs the 315964800 offset plus leap seconds (18 as of writing). Getting this wrong shifts every day-length calculation and is exactly the kind of error that looks plausible.

## Steps

1. Implement the callback; set the RTC epoch from it.
2. Convert: GPS → UTC using offset + leap seconds. **Put the leap-second constant somewhere obvious with a dated comment** — it changes, rarely, and silently.
3. Set the clock-valid flag and persist the epoch to `.noinit`.

## Done when

- [ ] Conversion correct against known reference values.
- [ ] The leap-second constant is documented and dated.
- [ ] Clock-valid set only on real acquisition.
