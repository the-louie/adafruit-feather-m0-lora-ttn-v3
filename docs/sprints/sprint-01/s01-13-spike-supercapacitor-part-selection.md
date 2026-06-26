# S01-13 — Spike supercapacitor part selection

**Estimate:** 1-2 h
**Backlog item:** TODO #11
**Depends on:** none
**Needs hardware:** no

## Context

The supercap sits in parallel with a 1S pack, so it lives on a 3.0–4.2 V rail. Standard supercaps are rated 2.7 V. No part has been selected.

Its leakage (µA to tens of µA) also counts against the sleep budget alongside the 15 µA INA219 — and the sleep budget is already the dominant term (TODO #11).

## Steps

1. Select a part rated above 4.2 V. A 5.5 V module is two 2.7 V cells in series with half the capacitance — size accordingly.
2. Size against the real load: ~120 mA for ~50 ms of TX, plus the RX windows.
3. Take the leakage figure from the datasheet and add it to the sleep budget.
4. Address inrush on first connection — a large cap across a fresh pack is a spark and a stressed cell.

## Done when

- [ ] Part selected: voltage rating, capacitance, ESR, leakage.
- [ ] Leakage added to the sleep budget.
- [ ] Inrush addressed or explicitly accepted.
