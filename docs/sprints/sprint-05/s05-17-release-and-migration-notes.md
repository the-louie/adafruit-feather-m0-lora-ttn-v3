# S05-17 — Release and migration notes

**Estimate:** 1-2 h
**Backlog item:** TODO #—
**Depends on:** S05-16
**Needs hardware:** no

## Context

Two protocol versions, changed counter semantics, a new variant, and a data correction — anyone reading this data later needs to know what changed and when.

## Steps

1. Summarise: idle fix, counter semantics, fast-flush fix, solar variant, decoder v7.
2. State the data implications plainly: readings before the idle fix are one interval late; sequence means something different after the flash.
3. Give dates and f_cnt boundaries so historical data can be interpreted per unit.

## Done when

- [ ] Release notes committed.
- [ ] Data implications stated in terms a future analyst can act on.
