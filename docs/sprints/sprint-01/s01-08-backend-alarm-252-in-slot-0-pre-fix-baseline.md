# S01-08 — Backend alarm 252 in slot 0 pre-fix baseline

**Estimate:** 1-2 h
**Backlog item:** TODO #10
**Depends on:** none
**Needs hardware:** no

## Context

The one-interval lag cannot be confirmed from current telemetry: no reboots in the capture window and no DEV units, so the 85 °C → sentinel-252 signature has never had a chance to appear.

This alarm must exist **before** the item 1 fix ships, so the next natural reboot captures the pre-fix baseline. Ship the fix first and the evidence is gone for good.

## Steps

1. Rule: any FPort 10 uplink with temperature slot 0 == 252.
2. Also alarm on f_cnt reset, so reboots are visible at all — the capture window contains none, which is itself worth knowing.
3. Correlate: a reboot followed by 252-in-slot-0 confirms the lag mechanism in the field.

## Done when

- [ ] Alarm live **before** any firmware change ships.
- [ ] Reboots independently visible.
- [ ] Baseline documented, even if it is 'no reboots observed yet'.
