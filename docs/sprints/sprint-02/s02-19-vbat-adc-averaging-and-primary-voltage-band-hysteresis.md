# S02-19 — VBAT ADC averaging and primary voltage-band hysteresis

**Estimate:** 2 h
**Backlog item:** TODO #1, #8
**Depends on:** S02-13
**Needs hardware:** no

## Context

**The voltage bands are bare thresholds and will dither — this is live in production now.** The A7 divider gives a 6.45 mV LSB (10-bit, 3.3 V ref, x2) and a single SAMD21 ADC sample carries ~+/-13-19 mV of noise. A pack within that of a band edge flips `voltage_offset` **every wake**.

`gisebo-04` reads 5.233 V today and is drifting toward the 5.00 V edge. When it arrives, its interval will thrash 4<->5 (30 min <-> 60 min) on ADC noise alone. The season machine got 1 C of hysteresis for exactly this reason; the voltage ladder never did.

Two mitigations, complementary rather than alternative: averaging narrows the noise at source, hysteresis absorbs what is left.

## Steps

1. **Average the ADC.** `getBatteryVoltage()` (`:26-30`) takes two dummy reads then **one** real sample. Average 16 - microseconds inside a wake that now deliberately spends 750 ms (S02-01), and it cuts noise 4x. Keep the two dummy reads; they let the sampling cap settle through the 100k/100k divider and that reasoning still holds.
2. **Add hysteresis**, rule: **degrade at the nominal edge, improve at nominal + 50 mV.** Asymmetric on purpose - react promptly to a failing pack, recover reluctantly. The protective response is never delayed, and 50 mV comfortably exceeds the +/-19 mV noise band.
   ```c
   #define VOLTAGE_HYST_V 0.05f
   static uint8_t voltageOffset(float v, uint8_t prev, const float edge[3]) {
     for (uint8_t i = 0; i < 3; i++) {
       float e = edge[i] + ((prev > i) ? VOLTAGE_HYST_V : 0.0f);
       if (v >= e) return i;
     }
     return 3;
   }
   ```
3. `voltage_offset` is now **latched state**, not a pure function of `vbat`. It must persist across wakes and belongs in `.noinit` beside `current_season_state` (S03-04). Until `.noinit` lands in sprint 03, hold it in a plain static and accept that a reset re-derives it - a one-wake transient, not a defect.
4. Land this as its **own commit**, after S02-13's identical-behaviour refactor. It is a deliberate behaviour change and its diff should be readable as one.

## Done when

- [ ] VBAT averaged over 16 samples; dummy reads retained.
- [ ] Hysteresis applied to the primary bands, degrading at nominal and improving at +50 mV.
- [ ] `voltage_offset` latched and ready to move into `.noinit`.
- [ ] Committed separately from the refactor.
