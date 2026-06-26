# S01-18 — Update cursor skills with confirmed defects

**Estimate:** 1-2 h
**Backlog item:** TODO #—
**Depends on:** S01-10, S01-11
**Needs hardware:** no

## Context

`.cursor/skills/master-plan/SKILL.md` and `domain-knowledge/SKILL.md` are the authoritative design rules, consulted before any code change. They currently describe the fast-flush and the sequence field as working.

## Steps

1. master-plan: correct the fast-flush description; record the uplink-counter semantics.
2. domain-knowledge: add the `ArduinoLowPower` second-granularity trap under the LMIC/hardware notes. It is exactly the class of thing that skill exists to prevent.
3. Raise the gitignore question: `.cursor` is in `.gitignore`, so these load-bearing skills exist only on one machine and are one `git clone` from being lost.

## Done when

- [ ] Both skills corrected.
- [ ] The ArduinoLowPower trap recorded where the next person will look for it.
- [ ] The `.cursor` gitignore risk raised with the team.
