# No MPPT — decided 2026-07-17 (S01-12)

## Decision

The panel feeds the Feather's USB pin through a Schottky and the INA219, charging via the onboard MCP73831. **No external charger, no input-voltage regulation.** The topology stands as originally designed — no rework.

## It will motorboat, and that is accepted

0.15 W at 5 V is ~30 mA. The MCP73831 is set to 100 mA and will drag the panel off its knee until it browns out, releases, recovers, and restarts. Expect to lose perhaps 30–50% of harvest — an **estimate**, never measured.

Accepted for four reasons:

- **There is no energy case.** ~10x claimed summer surplus and 500 days of dark-spell reserve. Halved harvest still leaves ~5x margin. This was never an energy decision.
- **The healthy-battery gate already covers the signal risk.** Motorboating only happens while *actively charging*; a full pack terminates the charger, the panel goes unloaded, and the bus sits cleanly at Voc. The solar bonus is gated on `voltage_offset == 0` — a near-full pack — so **the corrupted EWMA and the decision that consumes it never overlap.** That gate was chosen to stop sun and battery signals fighting; it happens to cover this too.
- **The INA219's own averaging is a free mitigation.** 128 samples takes ~68 ms and fits inside the 750 ms Dallas window. A single read per wake would point-sample an oscillation, catching it mid-collapse or mid-recovery at random, and the accumulator would integrate coin flips — biased unpredictably, not merely noisy.
- **An external charger is not a drop-in.** It means `panel → INA219 → charger → BAT pin`, bypassing the onboard charger, plus two chargers on one pack whenever USB is plugged in DEV. Real complexity on a post at a lake, bought against an unmeasured estimate.

## Accepted costs

- **The harvest accumulator carries an error bar**, quantified in S07-06. That weakens the pack-health trend — the only replacement-planning signal, since the supercap deliberately hides TX sag and internal resistance cannot be inferred from a 50 ms transmit. Given cells are already consumable on a 5–10 year cycle, this mostly costs the "or more often if needed" half.
- **The fouling alarm must gate on a full pack** (S07-06), or clarity reads low every morning during charging and false-positives.
- **There is no hardware fallback.** By decision.

## What would reopen it

**S07-05 only.** It measures the motorboat period against the ~68 ms averaging window. If the oscillation is much slower — a large input capacitor could easily make it hundreds of milliseconds — the averaging catches a fraction of one cycle and the accumulator is still integrating coin flips.

If that happens the remedies are **longer averaging, multiple spaced reads per wake, or a wider error bar** — not a charger.

## Rejected options — recorded so nobody re-runs the search

- **bq24074** — the textbook answer (VIN_DPM holds the panel near Vmp, TS pin allows temperature-qualified charging). **No amazon.se listing.** In Sweden it is an Electrokit/Digikey/Mouser part.
- **CN3065** — right chip for a 5 V panel on paper: 4.4–6 V input, 1S li-ion, marketed with solar input-voltage regulation. Several amazon.se listings. **But the regulation loop was never confirmed from the Consonance datasheet**, and that loop is the entire property being bought. If it is marketing over a plain linear charger, the part is pointless.
- **CN3791** — genuine switching MPPT with a resistor-set setpoint. **Trap: the amazon.se listings are 12 V variants**, which would try to hold a 5 V panel at a setpoint it can never reach and never charge at all. "MPPT" in the title does not mean it fits the panel.
- **Waveshare Solar Power Manager (D)** — 6–24 V input. Wrong for a 5 V panel.

**Stock could not be verified.** Four automated fetch attempts returned HTTP 503 or JavaScript shells; amazon.se renders availability client-side and blocks bots. Listings exist; stock is unconfirmed.
