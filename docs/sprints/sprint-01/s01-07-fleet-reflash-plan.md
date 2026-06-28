# S01-07 — Fleet reflash plan

**Estimate:** 1-2 h
**Backlog item:** TODO #14
**Depends on:** S01-05, S01-06
**Needs hardware:** no

## CLOSED — moot, 2026-07-17

**No fleet reflash happens.** This task assumed deployed units would be upgraded to v7. They will not:

- **gisebo-01** is production and **frozen**, then **retired** — gisebo-05 replaces it. It keeps v6 to its grave.
- **gisebo-02–04** are uncommissioned test devices. gisebo-04 is in a fridge running a cold test of primary lithium and **must not be disturbed** — reflashing it would destroy a months-long experiment that is currently answering the per-wake-versus-quiescent question for free.
- **gisebo-05** is new. It is born on v7 with its own decoder.

So the v6→v7 counter-semantics transition — the whole reason this task existed — **never arises**. Nothing ever reinterprets old bytes.

It also removes what was the plan's largest risk: the first board to run new firmware is a **bench board**, not a production board on a post at a lake.

The cutover that *does* happen is sprint 08, and it is a device swap, not a reflash.

## Original context (superseded)

Two units, two protocols, one decoder, one firmware with no source. Every backlog item that says 'deployed units need a reflash' depends on this.

And with no test devices, the first unit to receive new firmware **is** a production unit. That needs stating, not glossing.

## Steps

1. Decide the target: both units on current v6 + fixes.
2. Sequence against the item 2 decoder change — semantics change without a layout change, so an un-reflashed unit reads as an erratic uplink counter.
3. Decide: does the decoder tolerate both semantics during transition, or is the fleet flashed first?
4. Record physical access constraints — post-mounted at a lake; a reflash is a site visit.
5. State the risk explicitly: with no bench, firmware goes from compiler to production in one step.

## Done when

- [ ] Reflash order and decoder-change sequencing decided.
- [ ] Site-access constraints recorded.
- [ ] The no-bench risk written down and accepted by name.
