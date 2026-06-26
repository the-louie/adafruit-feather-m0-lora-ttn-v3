# S01-07 — Fleet reflash plan

**Estimate:** 1-2 h
**Backlog item:** TODO #14
**Depends on:** S01-05, S01-06
**Needs hardware:** no

## Context

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
