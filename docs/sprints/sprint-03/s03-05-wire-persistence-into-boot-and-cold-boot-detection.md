# S03-05 — Wire persistence into boot and cold-boot detection

**Estimate:** 2 h
**Backlog item:** TODO #5
**Depends on:** S03-04
**Needs hardware:** no

## Context

Without this, every join-failure reset restarts at Summer/index 2 and re-walks the season machine one step per uplink, with solar history empty — in winter, precisely when joins fail most and the wrong policy costs most.

## Steps

1. In `setup()`: validate, then either restore or cold-init.
2. Increment the boot counter on every `setup()` run — cold or soft.
3. Set the cold-boot flag when magic/version fails, so the two are distinguishable in telemetry.
4. Ensure `firstUplinkAfterJoin` (S02-04) re-arms correctly on both paths — a reset re-joins, so it should.

## Done when

- [ ] State restores across a soft reset, cold-inits otherwise.
- [ ] Boot counter increments on every setup.
- [ ] Cold boot and soft reset are distinguishable.
