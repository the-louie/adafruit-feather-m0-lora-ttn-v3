# S08-06 — Site-visit plan and rollback

**Estimate:** 2 h
**Backlog item:** — (cutover; see `docs/sprints/sprint-08/README.md`)
**Depends on:** S08-04
**Needs hardware:** no

## Context

The only irreversible step. The site is a post at a lake; if gisebo-05 fails there, the answer may be another visit.

## Steps

1. Confirm gisebo-05 has passed S08-04 before anyone drives anywhere.
2. Plan the physical work: mount, panel orientation (true south, vertical), sensor depth (**0.5–1 m — this is the season-driving surface sensor**), cable routing.
3. **Rollback: keep gisebo-01 intact and retrievable.** Do not decommission it in the same visit — bring it home working. If gisebo-05 fails in the first week, gisebo-01 goes back rather than the site going dark.
4. Take the bench kit: if the INA219 has come loose in transit, the probe boots the primary policy and parks the unit at a 7-day interval. Catch that on site, not from a dashboard a fortnight later.

## Done when

- [ ] Swap plan written, including sensor depth and panel orientation.
- [ ] gisebo-01 returns intact and retrievable, not decommissioned in place.
- [ ] The probe-misdetect check is on the on-site checklist.
