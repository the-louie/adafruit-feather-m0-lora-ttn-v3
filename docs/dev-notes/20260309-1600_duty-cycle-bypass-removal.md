# Duty cycle bypass removal

## Summary

Removed the manual LMIC duty cycle bypass so the firmware respects ETSI EU868 duty cycle limits in production.

## Changes

- **adafruit-feather-m0-lora-ttn-2.ino**: Deleted the four lines that set `LMIC.globalDutyAvail = 0` and reset `LMIC.bands[i].avail = 0` for all bands immediately before each uplink. The send path now goes directly from payload build to LED on, `txComplete = false`, and `LMIC_setTxData2()`. Blocking wait for `EV_TXCOMPLETE`, timeout handling, and post-TX logic are unchanged.

- **.cursor/skills/master-plan/SKILL.md**: Replaced the subsection "The Frozen Time Duty Cycle Hack" with "Respect LMIC Duty Cycle". The master plan now states that the application respects LMIC duty cycle and does not override `globalDutyAvail` or `bands[].avail`, and that after long deep sleep the first transmission may be delayed until LMIC permits it.

- **.cursor/skills/domain-knowledge/SKILL.md**: Updated the duty cycle bullet under LoRaWAN/TTN basics to state that the project respects LMIC duty cycle and does not manually reset availability timers. Removed the reference to the "frozen time duty cycle" workaround. Updated the Relation to master-plan paragraph to say "respect LMIC duty cycle" instead of "duty cycle hack".

## Rationale

Resetting duty availability before every TX bypassed ETSI EU868 limits and could cause gateway/network-server drops or device bans. The firmware now relies on LMIC to enforce duty cycle; the first TX after a long deep sleep may be delayed until LMIC allows it.

## Verification

- Only the send path in the .ino previously touched `globalDutyAvail` or `bands[].avail`; no other code depended on the reset.
- No remaining references to the duty cycle hack, Frozen Time, or manual wipe in code or skills.
- TODO.md and TODO-summarized.md do not exist in the repo; no TODO cleanup required.
