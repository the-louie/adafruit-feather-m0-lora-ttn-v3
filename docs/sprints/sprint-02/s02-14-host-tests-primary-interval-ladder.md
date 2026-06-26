# S02-14 — Host tests primary interval ladder

**Estimate:** 2 h
**Backlog item:** TODO #4
**Depends on:** S02-13
**Needs hardware:** no

## Context

Pin the current behaviour before the solar variant arrives to complicate it. These tests are the contract that says the refactor changed nothing.

## Steps

1. Test each season base: Summer 4, Fall/Spring 5, Winter 7.
2. Test each voltage offset band: ≥5.0 → 0, ≥4.3 → 1, ≥3.5 → 2, below → 3.
3. Test the clamp: Winter + critical battery must give 10, not 11.
4. Replay real production values: 5.768 V at 16.8 °C must give index 4 — that is gisebo-01's actual behaviour and a real-world anchor for the test suite.

## Done when

- [ ] All bases and bands covered.
- [ ] Clamp verified.
- [ ] The gisebo-01 production case passes.
