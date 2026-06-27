# S02-19 — VBAT ADC averaging and primary voltage-band hysteresis

**Estimate:** 2 h
**Backlog item:** TODO #1, #8
**Depends on:** S02-13
**Needs hardware:** no

## Context

**The voltage bands are bare thresholds and will dither.** The A7 divider gives a 6.45 mV LSB (10-bit, 3.3 V ref, ×2) and a single SAMD21 ADC sample carries ~±13–19 mV of noise. A pack sitting within that of a band edge flips `voltage_offset` **every wake**.

**Correction — this is NOT live in production**, contrary to an earlier draft of this task. That draft said "gisebo-04 reads 5.233 V and is drifting toward the 5.00 V edge". Both halves are wrong:

- **gisebo-04 runs V5 firmware**, which has a *fixed* 5-minute interval (confirmed from its real uplink timing: median gap 30.2 min = 6 × 5.03 min). No dynamic interval means no voltage ladder means nothing to dither. And V5 is never being reflashed.
- **gisebo-01 reads 5.768 V** — 0.77 V clear of the 5.00 V edge, nowhere near dithering.

So no deployed device has this defect today. **It matters because of gisebo-05.** That unit is solar, and its 3.85 V edge *gates the solar bonus*: a li-ion pack sits on the 3.6–3.9 V plateau for most of its life, so it will spend real time within ±19 mV of that edge — and each flip swings the interval 2↔4, i.e. 5 min ↔ 30 min, wake to wake. The primary bands get the same treatment because the mitigation is shared code (S04-07 reuses this), not because a primary unit needs it today.

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
