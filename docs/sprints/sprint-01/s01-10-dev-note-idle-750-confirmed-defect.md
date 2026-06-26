# S01-10 — Dev-note idle 750 confirmed defect

**Estimate:** 1-2 h
**Backlog item:** TODO #1
**Depends on:** none
**Needs hardware:** no

## Context

Write up the confirmed defect with its evidence while it is fresh. Convention: one dated dev-note (`YYYYMMDD-HHMM_slug.md`) covering summary, rationale, verification.

## Steps

1. Record the source proof: `setAlarmIn` does `rtc.setAlarmEpoch(now + millis/1000)` — integer division, so `idle(750)` sets the alarm to the current second.
2. Record the branch identification: both units transmit, so it is not hanging; therefore it returns early, and the DS18B20 read returns the previous conversion — a **one-interval lag**.
3. Record what could **not** be checked and why: no reboots in the window, no DEV units. State it, so the next person does not re-derive a check that cannot run.
4. Cross-reference `20260309-1700_usb-serial-stability-lowpower-idle-by-runmode.md` — the USB symptom described there *was* this bug, diagnosed and worked around for DEV only.

## Done when

- [ ] Dev-note committed with the source quote and the data evidence.
- [ ] The impossible checks documented as impossible.
- [ ] The 2026-03-09 dev-note cross-referenced.
