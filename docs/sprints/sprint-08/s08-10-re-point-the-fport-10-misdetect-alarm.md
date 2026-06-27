# S08-10 — Re-point the FPort 10 misdetect alarm

**Estimate:** 1 h
**Backlog item:** — (cutover; see `docs/sprints/sprint-08/README.md`)
**Depends on:** S08-09
**Needs hardware:** no

## Context

S01-09 alarms on FPort 10 with battery < 4.5 V — a solar board that failed to detect its INA219 and booted the primary policy, parking itself at a 7-day interval.

After cutover that alarm gets **much sharper**: gisebo-01 is gone, so **no device should ever legitimately use FPort 10 again**. Any FPort 10 traffic at all is a misdetect.

## Steps

1. Once gisebo-01 is retired, simplify the rule: **any** FPort 10/20 uplink is an alarm — drop the battery-voltage qualifier, which only existed to avoid firing on gisebo-01.
2. Keep the reasoning at the alarm. Someone will eventually deploy a primary-cell unit and need to know why FPort 10 alarms.

## Done when

- [ ] Alarm re-pointed and simplified.
- [ ] Rationale documented at the alarm.
