# Sprint 99 — Operator backlog

**Status:** parked, awaiting the operator.

Tasks that could not be closed autonomously. **None failed** — each needs an access, a decision, or a physical object that the agent does not have. Listed with exactly what unblocks them, so the first ten minutes back are productive.

## Needs backend access (telegraf / influx / grafana)

| task | what it needs | urgency |
|---|---|---|
| **S01-08** | 252-in-slot-0 alarm + f_cnt reset alarm | **Window is closing.** gisebo-01 keeps pre-fix firmware until it retires, so it is the only device that can ever show the lag signature in production. TTN retains ~3 days. If the alarm is not live when its next natural reboot happens, that evidence is gone. The bench (S06-13) is the definitive test, so this is insurance — but it expires. |
| **S01-09** | device-based misdetect alarm (`SOLAR_DEVICES` on FPorts 10/20) + battery <4.5 V backstop | Build before gisebo-05 exists; it never fires until there is something to fire about, and it is armed the instant a transit-loosened INA219 shows up. Spec is written and ready to implement. |

## Needs a decision or an order status

| task | what it needs |
|---|---|
| **S01-15** | Firm ETA on the first order (Feather, DS18B20, INA219, panel, pack). Second order is now unblocked: **1 F 5.5 V supercap module** (S01-13) and **plain cells + one pack-level PCM** (S01-14) are both decided. |

## Blocked on hardware — sprints 06 and 07 entire

24 tasks. Not failures: they need a board on a bench and, for solar, a panel and a PSU. Sprint 05's readiness review (S05-18) dates or parks them.

The highest-value hour in the whole plan lives here: **S06-13** — flash pre-fix firmware, cold boot, and finally observe the 252 signature that 139 production uplinks could not show, because no reboot has occurred in the entire retained window.

## Blocked on a site visit — sprint 08 entire

12 tasks. The cutover swaps gisebo-05 for gisebo-01 at a post on a lake. Rehearsable from a bench (S08-04) but not executable from here.

## Questions the operator should answer

1. **Is the inrush question real?** A 0.5 F supercap across a fresh 1S pack is a short until charged. Series resistor (compromises the low-ESR path the cap exists for), or a documented connect procedure? — S01-13
2. **Does the telegraf schema handover have an owner?** S08-01 produces the artifact; someone outside this repo has to consume it.
3. **gisebo-05 does not exist in TTN yet.** Creating it is safe — the webhook discards what does not fit — but it was not created without being asked.
