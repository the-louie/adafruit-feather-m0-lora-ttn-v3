# Supercap sizing and over-discharge protection (S01-13, S01-14)

## S01-13 — Supercapacitor

**Recommendation: a 1 F, 5.5 V module (≈0.5 F effective). Do not go smaller.**

### Size for the cold RX window, not the TX burst

The design has always described the supercap as covering "the radio's 120 mA TX spike". That framing is right about *sag* — sag is `I × ESR`, and TX is the high-current event:

| pack ESR | TX sag (120 mA) | RX sag (20 mA) |
|---|---|---|
| 0.15 Ω (li-ion 2P, 20 °C) | 18 mV | 3 mV |
| 1.0 Ω (−10 °C) | **120 mV** | 20 mV |
| 3.0 Ω (−20 °C, aged) | **360 mV** | 60 mV |

But it is wrong about *sizing*. The TX burst is 50 ms and **6 mC**; the RX windows are ~3 s at ~20 mA and **60 mC** — ten times the charge. In extreme cold, where the pack's ESR is high enough that it cannot supply the sustained draw, the cap ends up carrying the RX window too:

| event | charge | C needed for 0.2 V sag |
|---|---|---|
| TX burst alone | 6 mC | **0.03 F** |
| RX windows | 60 mC | **0.33 F** |

**11× difference.** A 0.1 F part would cover the TX burst — the thing everyone talks about — and fail the cold RX window, which is the case the supercap exists for in the first place.

### The 5.5 V module halves the capacitance

Standard supercaps are 2.7 V. A 1S li-ion rail reaches 4.2 V, so the part must be a **5.5 V module — two 2.7 V cells in series, which halves the marked capacitance.** A "1 F" 5.5 V module is **0.5 F effective**, comfortably above the 0.33 F needed. Size the *module*, not the cell.

### Leakage is affordable

| part | leakage | % of the ~290 µA sleep budget |
|---|---|---|
| [KEMET FU0H 5.5 V](https://content.kemet.com/datasheets/KEM_S6023_FU0H.pdf) | ~5 µA | 1.7% |
| generic 5.5 V module | ~10 µA | 3.4% |
| warm worst case | ~30 µA | 10% |

Quiescent already dominates, so this is noise. **But leakage rises sharply with temperature**, and a sealed dark box behind a south-facing panel in July is the hot case — use the datasheet self-discharge curve at 60 °C, not 25 °C.

Candidates with published data: [KEMET FU0H series](https://content.kemet.com/datasheets/KEM_S6023_FU0H.pdf) and [Abracon ADCH-S05R5S](https://abracon.com/datasheets/ADCH-S05R5S.pdf), both 5.5 V.

### Not resolved here

**Inrush.** A 0.5 F cap across a fresh pack is an effective short until it charges. Needs either a series resistor (which compromises the low-ESR path the cap exists to provide) or a documented connect procedure. Flagged for the build.

## S01-14 — Over-discharge protection

**Recommendation: plain cells with ONE pack-level PCM across the 2P bank. Not protected cells.**

### The cutoff has a correct window and standard parts land in it

```
  3.45 V   VOLTAGE_CRITICAL_V     <- firmware reacts first (max interval)
  3.40 V   Feather brownout       <- MCU stops; leakage continues
  2.4-2.8  typical 1S PCM cutoff  <- MUST land here
  ~2.5 V   li-ion damage onset
```

A standard 1S PCM sits correctly: **below** the brownout, so firmware and MCU both get to act first, and **above** the damage threshold, so the cell survives a long dark spell after the MCU has given up. Firmware cannot help once it is off — that gap is exactly what the PCM is for.

### Why not protected cells

This is a **2P** pack. Protected 18650s each carry their own PCM, and putting two in parallel is a known bad idea: if one trips first the other inherits the full load and trips shortly after, and on recovery the cells can fight each other through mismatched protection states.

**One PCM across the bank** sees the true pack voltage and current, trips once, and recovers once.

### Add the PCM's quiescent to the budget

A PCM draws its own standby current (typically a few µA). It joins the supercap's leakage and the INA219's 15 µA against a budget already dominated by ~290 µA of board quiescent — affordable, but it should be counted rather than assumed.
