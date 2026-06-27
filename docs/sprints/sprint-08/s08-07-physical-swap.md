# S08-07 — Physical swap

**Estimate:** 2 h
**Backlog item:** — (cutover; see `docs/sprints/sprint-08/README.md`)
**Depends on:** S08-06
**Needs hardware:** YES

## Context

Execute. gisebo-01 out, gisebo-05 in.

## Steps

1. Note the exact time — S08-05's annotation needs it.
2. Confirm gisebo-05 joins from the site before leaving. A join failure at a post is a wasted trip; the PROD path retries for 3 minutes then sleeps 15 and resets, so wait it out rather than guessing.
3. Confirm the first uplink arrives on **FPort 11**. FPort 10 would mean the INA219 was not detected and the unit is about to park itself at a 7-day interval.
4. Bring gisebo-01 home powered down but intact.

## Done when

- [ ] gisebo-05 joined and uplinking on FPort 11, verified on site.
- [ ] Cutover timestamp recorded.
- [ ] gisebo-01 retrieved intact.
