# S05-08 — Wire all vectors into the harness with coverage

**Estimate:** 2 h
**Backlog item:** TODO #3
**Depends on:** S05-07
**Needs hardware:** no

## Context

The point of the whole exercise. `doc/test-payloads.md` rotted because it was prose pretending to be tests.

## Steps

1. Every vector runs in the S01-04 harness.
2. Report branch coverage: three sentinels, the `v <= 200` path, both battery clamps, all four FPorts, all three payload lengths (8/9/15), interval index 0 and >10 edges, every status flag.
3. Wire into whatever CI exists. If none exists, say so in the dev-note — an unrun test suite is the thing that caused this.

## Done when

- [ ] Every vector executes.
- [ ] Coverage reported and complete.
- [ ] CI wired, or its absence recorded as a risk.
