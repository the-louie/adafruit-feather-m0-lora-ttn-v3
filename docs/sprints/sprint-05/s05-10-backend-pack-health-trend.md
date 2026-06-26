# S05-10 — Backend pack health trend

**Estimate:** 2 h
**Backlog item:** TODO #10
**Depends on:** S04-15
**Needs hardware:** no

## Context

Cells are consumable on a 5–10 year cycle — which only works if you can see them ageing from the office.

The obvious approach is closed off: **the supercap deliberately hides the TX sag**, so internal resistance cannot be inferred from a 50 ms transmit. The only available signal is resting voltage trending down over months while harvest stays normal. That is a backend inference and there is no firmware substitute.

## Steps

1. Trend resting battery voltage per unit over months.
2. Correlate against the harvest accumulator: falling voltage **with** normal harvest means the pack is ageing; falling voltage **with** falling harvest means the panel or the weather.
3. Alarm on a sustained downward trend that harvest does not explain.
4. This is the whole replacement-planning mechanism — document it as such.

## Done when

- [ ] Trend computed per unit.
- [ ] Pack ageing distinguished from poor harvest.
- [ ] Documented as the replacement trigger.
