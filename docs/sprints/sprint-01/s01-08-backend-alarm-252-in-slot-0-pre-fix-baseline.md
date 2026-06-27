# S01-08 — Backend alarm 252 in slot 0 pre-fix baseline

**Estimate:** 1-2 h
**Backlog item:** TODO #10
**Depends on:** none
**Needs hardware:** no

## Context

The one-interval lag cannot be confirmed from current telemetry: no reboots in the capture window and no DEV units, so the 85 °C → sentinel-252 signature has never had a chance to appear.

**Re-checked 2026-07-17 against the full storage window** (139 uplinks, 2026-07-15 → 07-17 — everything TTN retains, 32 more than the original dump): **still zero f_cnt resets on either unit, still zero 252 sentinels.** Both units have run uninterrupted for the entire retained history. The `{1, 7, 13}` sequence signature holds across all 139.

This is not evidence of absence — it is absence of evidence. There has been no reboot to observe. Which is precisely why this alarm must exist *before* the fix ships: the next natural reboot is the only chance to capture the pre-fix baseline, TTN retains roughly 3 days, and nobody will be watching at the moment it happens.

This alarm must exist **before** the item 1 fix ships, so the next natural reboot captures the pre-fix baseline. Ship the fix first and the evidence is gone for good.

## Steps

1. Rule: any FPort 10 uplink with temperature slot 0 == 252.
2. Also alarm on f_cnt reset, so reboots are visible at all — the capture window contains none, which is itself worth knowing.
3. Correlate: a reboot followed by 252-in-slot-0 confirms the lag mechanism in the field.

## Done when

- [ ] Alarm live **before** any firmware change ships.
- [ ] Reboots independently visible.
- [ ] Baseline documented, even if it is 'no reboots observed yet'.
