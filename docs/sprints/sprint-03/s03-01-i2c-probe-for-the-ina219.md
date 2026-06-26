# S03-01 — I2C probe for the INA219

**Estimate:** 2 h
**Backlog item:** TODO #4
**Depends on:** sprint-02
**Needs hardware:** no

## Context

Runtime variant selection: INA219 found at 0x40 → solar policy; absent → primary-cell. One firmware image for every board, consistent with the compile-once-flash-everywhere goal in `docs/generate-keys-from-feather-serial.md`.

## Steps

1. `Wire.begin()`, then probe 0x40 for an address ACK.
2. The INA219 has no ID register (the INA226 does), so add a config-register sanity read — expect the reset default — rather than trusting a bare ACK.
3. **Retry: 3 attempts, 50 ms apart, before concluding absent.** One transient I2C glitch would otherwise select the primary policy for the whole session, pinning a solar unit at a 7-day interval until something resets it. The probe runs once per boot and a boot is rare - there is no reason to trust a single attempt with that outcome. 100 ms is nothing in a `setup()` that already spends `delay(5000)` waiting for the radio.
4. Guard against a hung bus: a stuck-low SDA must not block boot forever. A bus that never releases should be treated as 'absent', loudly (task 02).
4. Select and instantiate the policy from the result.

## Done when

- [ ] Probe selects correctly when the INA219 is present and when it is absent.
- [ ] A hung bus cannot hang boot.
- [ ] The config-register sanity check is in, not just an ACK.
