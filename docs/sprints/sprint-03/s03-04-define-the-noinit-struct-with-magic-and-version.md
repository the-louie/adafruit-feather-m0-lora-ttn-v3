# S03-04 — Define the noinit struct with magic and version

**Estimate:** 2 h
**Backlog item:** TODO #5
**Depends on:** sprint-02
**Needs hardware:** no

## Context

A soft reset does not physically clear SRAM — only the C runtime zeroes `.bss`. So `__attribute__((section(".noinit")))` survives `NVIC_SystemReset()` at zero flash cost, fully consistent with the no-FlashStorage rule in master-plan.

**The layout version is not optional.** A firmware upgrade that changes the struct must not read the old layout as valid, or the device resumes from garbage — strictly worse than a cold boot.

## Steps

1. `persist.h`: struct with magic word + layout version + payload.
2. Contents:
   ```c
   struct PersistState {
     uint32_t magic;       // fixed constant
     uint16_t version;     // bump on ANY field change
     uint16_t crc;         // over everything below
     // ---- body ----
     uint8_t  seasonState, voltageState, currentIntervalIndex, uplinkCounter, bootCounter;
     float    surfaceTempC, sunEwma;
     uint32_t rtcEpoch;
     uint16_t harvestMilliAmpHours;
   } __attribute__((section(".noinit")));
   ```
   `voltageState` is required: S02-19's hysteresis makes `voltage_offset` latched state, not a pure function of `vbat`.
3. Validity: magic AND version AND **crc(body)**. Any failing → cold boot.
   **Magic + version alone is not enough.** They catch layout changes and clean cold boots; they do not catch corruption. This design leans on a physical claim — that SRAM survives a soft reset — and a brief power interruption decays RAM *partially*: long enough to corrupt the body, short enough to leave a 32-bit magic word standing. The result is a confident restore from garbage, which is the exact failure the version guard exists to prevent, arriving through a different door. S06-06 tests this on hardware; without a CRC there is no plan for when it fails.
4. Add a `static_assert` on `sizeof(PersistState)` so a field change nobody versioned fails the build rather than the field.
4. Document the rule loudly: **bump the version on any field change.**

## Done when

- [ ] Struct defined with magic and version.
- [ ] Validity requires both.
- [ ] The bump-on-change rule is documented where it will be seen.
