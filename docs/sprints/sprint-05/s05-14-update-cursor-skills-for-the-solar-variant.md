# S05-14 — Update cursor skills for the solar variant

**Estimate:** 2 h
**Backlog item:** TODO #—
**Depends on:** sprint-04
**Needs hardware:** no

## Context

master-plan and domain-knowledge are consulted before any code change. They currently describe a single-policy firmware.

They are also **gitignored** — `.cursor` is in `.gitignore` — so they exist on one machine only. That has been raised twice now (S01-18) and is worth escalating rather than repeating.

## Steps

1. master-plan: the three-way split, the policy interface, `.noinit` and why it does not violate the no-FlashStorage rule.
2. domain-knowledge: the INA219 charge-terminated behaviour, the `ArduinoLowPower` second-granularity trap, DeviceTimeReq and GPS epoch.
3. Escalate the gitignore question with a recommendation, not just a mention.

## Done when

- [ ] Both skills current.
- [ ] The gitignore risk escalated with a proposed resolution.
