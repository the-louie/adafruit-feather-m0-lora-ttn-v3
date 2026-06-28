# S01-09 — Backend alarm: a solar device on a primary FPort

**Estimate:** 1–2 h
**Backlog item:** TODO #10
**Depends on:** none
**Needs hardware:** no

## Context

The solar variant selects its policy by probing for the INA219 at 0x40. A dead sensor, a loose wire, or a hung bus makes a **solar board boot the primary-cell policy** — whose 5.0/4.3/3.5 V bands all sit above a full li-ion's 4.2 V. Every reading then scores `voltage_offset = 3` and the unit pins itself at interval index 10 — **7 days** — permanently.

A component fault silently decommissions the unit for a year. On gisebo-05 that fault would be at the production site, after gisebo-01 has already been retired. This alarm is the only thing between a loose connector and a dark site.

## The discriminator is the device, not the FPort and not the battery

An earlier draft alarmed on **FPort 10 with battery < 4.5 V**, and planned to simplify it to "any FPort 10" after cutover on the reasoning that gisebo-01's retirement would leave FPort 10 unused. **Both were wrong.**

Real traffic, 2026-07-17:

| device | FPort | uplinks |
|---|---|---|
| gisebo-01 | 10 | 20 |
| gisebo-04 | 10 | 119 |

**gisebo-04 is on FPort 10 too, runs V5, and is never being reflashed.** It will still be there long after gisebo-01 retires, so "any FPort 10" would alarm on it forever. And the battery-voltage qualifier was only ever a proxy for "this is not a legitimate primary unit".

The real invariant is simpler and needs no cutover: **gisebo-05 is a solar unit and must only ever use FPorts 11/21. If it appears on 10 or 20, its INA219 was not detected.** That is true today, true after cutover, and independent of what any other device does.

## Steps

1. **Primary rule — precise, no false positives possible:**

   ```
   device_id in SOLAR_DEVICES  AND  f_port in (10, 20)   ->  ALARM, high severity
   ```

   `SOLAR_DEVICES` starts as `{gisebo-05}`. Add to it as solar units are built.

   Build it **now**, before gisebo-05 exists. It simply never fires until there is something to fire about — and it is armed the instant the device joins, which is exactly when a transit-loosened INA219 would show up.

2. **Backstop rule — catches a solar unit nobody added to the list:**

   ```
   f_port in (10, 20)  AND  battery_v < 4.5   ->  ALARM
   ```

   No healthy 6 V primary pack sits below 4.5 V, and no li-ion ever reaches it, so the band between them is empty and this cannot false-positive on gisebo-01 (5.768 V) or gisebo-04 (5.233 V). It is a safety net for the case where the primary rule's device list goes stale — which it will, the first time someone builds a solar unit in a hurry.

3. Severity **high** on both. This is a silent-decommission detector, not a warning: the symptom is a unit that keeps reporting, keeps looking healthy, and goes quiet for a week at a time.

4. **Document the reasoning at the alarm**, not only here. Someone will eventually deploy a legitimate primary-cell unit on FPort 10 and need to understand why that is fine while gisebo-05 doing it is not.

## Done when

- [ ] Device-based rule live, with `SOLAR_DEVICES = {gisebo-05}`.
- [ ] Backstop battery rule live.
- [ ] Verified silent against current traffic — gisebo-01 at 5.768 V and gisebo-04 at 5.233 V must not fire either rule.
- [ ] Rationale documented at the alarm.
- [ ] A note in the solar build checklist: adding a solar device means adding it to `SOLAR_DEVICES`.
