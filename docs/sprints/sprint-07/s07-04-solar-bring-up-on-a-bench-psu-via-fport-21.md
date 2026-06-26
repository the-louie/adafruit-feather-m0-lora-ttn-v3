# S07-04 — Solar bring-up on a bench PSU via FPort 21

**Estimate:** 2 h
**Backlog item:** TODO #8
**Depends on:** S07-01
**Needs hardware:** YES

## Context

**Serial and real solar are mutually exclusive**: USB puts 5 V on the pin the panel feeds, so the Schottky blocks the panel and the INA219 reads 0 mA on a 5 V bus. Bring-up runs on a bench PSU with telemetry over the air.

## Steps

1. Current-limited bench PSU on the panel input, ~30 mA limit to imitate the panel.
2. Read telemetry on FPort 21. Do not expect serial.
3. Sweep the PSU to simulate dawn, noon, dusk, night; confirm the sun-presence EWMA tracks.
4. Confirm the `bus_mV > 3000` threshold discriminates day from night correctly at the real panel's voltages.

## Done when

- [ ] Solar path verified without USB.
- [ ] EWMA tracks a simulated day.
- [ ] The day/night threshold is right for the real panel.
