# Sprint 07 — Solar bring-up and field readiness

**Dates:** 2026-10-08 (Thu) → 2026-10-21 (Wed) — **provisional, gated on delivery**
**Capacity:** 1 developer, ~30 h effective
**Planned:** 12 tasks, ~24 h
**Status:** 🟡 Gated on delivery — INA219, panel and pack ordered; supercap and protection pending S07-08

## Goal

Bring up the solar path on real hardware, pay the costs the no-MPPT decision accepted, and get the fleet to a known state.

## The charger question is closed

**No MPPT — decided 2026-07-17** (S01-12). No external charger; the panel feeds the Feather's USB pin through a Schottky and the INA219, charging via the onboard MCP73831. The topology stands as originally designed, so there is no rework here.

It will motorboat — a ~30 mA panel against a 100 mA charger — costing perhaps 30–50% of harvest. That is affordable: ~10× summer surplus, 500 days of reserve. It was never an energy decision. And the signal risk is already covered, because motorboating only happens while actively charging, while the solar bonus only applies to a near-full pack that has terminated the charger. **The corrupted EWMA and the decision that consumes it never overlap.**

The mitigation is the INA219's own 128-sample averaging (~68 ms, inside the 750 ms Dallas window). **Task 05 is the one measurement that could reopen this**: if the motorboat period turns out much longer than 68 ms, the averaging catches a fraction of one cycle and the accumulator is still integrating coin flips. There is no hardware fallback by decision — the remedies would be longer averaging, multiple reads per wake, or a wider error bar.

Tasks 05 and 06 exist to pay the two accepted costs: quantify the harvest error bar, and gate the fouling alarm on a full pack.

## Working agreement

- **Commit in small batches**: one function + its test (if available) + its documentation, per commit.
- Every finding gets a dated dev-note, contradictions especially.

## Task index

| # | Task | Est | Item |
|---|---|---|---|
| 01 | Verify INA219 wiring address and calibration | 2 h | 8 |
| 02 | Verify the probe selects correctly when present | 1 h | 4 |
| 03 | Verify the probe fails correctly when disconnected | 2 h | 4 |
| 04 | Solar bring-up on a bench PSU via FPort 21 | 2 h | 8 |
| 05 | Characterise motorboating and validate the averaging | 2 h | 11 |
| 06 | Quantify the harvest error bar and gate the fouling alarm | 2 h | 10 |
| 08 | Second procurement round supercap and protection | 1 h | 11 |
| 09 | Verify the charge-terminated case end to end | 2 h | 8 |
| 10 | Multi-day solar soak | 2 h | 8 |
| 11 | Remediation buffer for solar findings | 4 h | 12 |
| 12 | Field deployment readiness review | 2 h | 14 |
| 13 | Dev-note solar bring-up findings | 2 h | 8 |

## Exit criteria

- The solar path works on real hardware, verified over the air rather than over serial.
- The INA219 averaging is proven sufficient against real motorboating, or an alternative is chosen — **not a charger**.
- The harvest error bar is quantified and documented where someone reading a pack-health trend will see it.
- **The disconnected-probe failure path has been run on hardware** — the difference between a loose connector being a minor fault and a unit going silent for a year.
- The fleet reflash is ready to execute.
