# Measuring solar input better than one 532 µs sample — research and decision

Date: 2026-07-29. Prompted by the first day of schema-2 panel profiles: the
wake-time sample and the true hourly mean disagreed by up to **63% in a single
hour** (13:33: sample 42.1 mA vs mean 115 mA), leaving `harvest_mah`
under-reported ~36% over four hours, **with the sign varying hour to hour** so
no fixed correction exists. Question: is there a structurally better way to
measure harvest, weighted (1) compatibility with the current hardware/firmware
— a hard gate, (2) robustness — the dominant metric, (3) ease of implementation
— tie-breaker only.

## The physics of the problem

Harvest is an integral; we sample the integrand. On a partly cloudy windowsill
the integrand swings 12–116 mA within one hour (measured, 2026-07-29 profile),
so ANY point-sampling scheme aliases. The only structural fixes are (a) sample
much faster than the cloud timescale, (b) integrate in hardware, or (c) measure
something that is itself already an integral. Everything below is one of those
three.

## Options investigated

### A. INA219 built-in features (100% compatible)

Confirmed from SBOS448G and TI's own product comparison: **the INA219 has no
charge or energy accumulator** — the register map ends at 05h, and TI's
positioning note for it says "no power accumulator" explicitly. Its two
relevant built-ins:

- **Averaging (BADC/SADC up to 128 samples = 68.1 ms)** widens the snapshot
  window from 532 µs to 68 ms. Against hour-scale cloud variation this changes
  nothing (aliasing dominates); it would smooth charger switching ripple only.
  It also changes the conversion time 128×, interacting with the CNVR budget
  (which would fault loudly rather than silently — item 18's property — but
  the churn buys nothing). **Assessed: no robustness gain for this problem.**
- **Continuous mode across the sleep interval** would integrate in firmware at
  wake… except the part draws **0.7–1 mA converting**, i.e. 16.8–24 mAh/day
  against a total harvest of 7–28 mAh/day. It would consume the quantity it
  measures. This is why `powerSave` is load-bearing. **Rejected on the
  project's own measured numbers.**

### B. Sibling-chip swap: INA226 / INA228 / INA229 (fails the gate)

The INA228/229 *do* have hardware charge and energy accumulators — but the
datasheet and TI E2E are explicit that **accumulation only runs in continuous
mode**; shutdown (≈5 µA) stops both conversion and accumulation, and triggered
mode leaves the ENERGY/CHARGE registers invalid because the device does not
track elapsed time. Continuous mode is ~640 µA — the same self-consumption
trap as A, in a nicer package. The INA226's deeper averaging (up to ~8.4 s
windows) is still a snapshot. All require replacing a soldered part and the
probe/calibration stack. **Fails compatibility, and would not solve the
problem if it passed.** Worth recording because "the INA228 has an energy
accumulator" is the obvious-sounding wrong answer.

### C. Dedicated coulomb counter (the textbook answer; fails the gate today)

LTC2941/2942/2944-class gas gauges integrate **analog charge through the sense
resistor continuously while the host sleeps**, at <100 µA quiescent (LTC4150
is the pulse-output variant). This is the industry-standard architecture for
exactly this problem — the integration happens in hardware, the MCU reads an
accumulated total over I2C whenever it wakes, and narrow spikes between
samples cannot be missed by construction. Notes from the survey: the LTC2941
is marked end-of-life; the LTC2942 adds voltage/temperature readout; the
LTC2944 extends the voltage range. **Highest robustness of any direct
measurement, but requires a new part on the board → fails today's gate.**
Recorded in TODO item 11 as the component to add IF a future board revision
happens AND true harvest accounting has become a requirement.

### D. Micro-wake chunked sampling in PROD (compatible, rejected on robustness)

The pattern used by the Hackaday SOL solar-sensing project: sleep in short
chunks, wake briefly, sample, sleep again (SOL: ~53 ms at ~12 mA per sample,
6–7 µA sleep floor). For us: slice `LowPower.deepSleep(interval)` into 60 s
chunks with an INA219 sample per chunk — the DEV profile sampler's quality in
PROD. Energy works out fine (~15 ms × ~10 mA × 1440/day ≈ 0.1 mAh/day, well
inside budget; the RTC alarm's 1 s granularity is respected by 60 s chunks).

**Rejected, and the reasoning is the point of this document.** The change
lives in the PROD sleep path — the one code region this project's history
proves cannot be verified from DEV *by construction* (`idle(750)` corrupted
PROD data for four months precisely because the DEV path is different code).
The metric it improves is secondary telemetry; the code it destabilises is the
core duty cycle of a field unit. With robustness as the dominant weight, a
measurable gain in a diagnostic number does not pay for risk in the sleep
path. Kept documented as the fallback if direct PROD harvest data ever becomes
a hard requirement before a board revision.

### E. The battery as the integrator (compatible; the recommendation)

The pack itself is a physically perfect coulomb counter: battery voltage is a
function of accumulated net charge, it integrates continuously at zero cost,
it cannot alias, and **it is already on the wire in every frame**. The known
objections, checked against the literature and this project's own data:

1. *Li-ion's plateau is flat.* True: ~3.90–3.50 V spans roughly 80–20% SOC
   (~400 mV over 60%), and a 50 mV error maps to 20–30% SOC — voltage is a
   poor *instantaneous* gauge. But the question `harvest_mah` exists to answer
   is a **trend**: is harvest ≥ consumption over days? A slope hides in far
   less noise than a level. The A7 path already averages 16 ADC samples
   (~±5 mV effective), the sample is taken at the same point in every wake
   cycle (open-circuit, pre-TX), so load offset cancels in the slope, and the
   temperature coefficient is a known, measured quantity in this project
   (+12.8 mV/°C on alkaline, smaller on li-ion, and the backend has the water
   temperature in the same frame to correct against).
2. *It measures net, not harvest.* Also true — and net energy balance is the
   actual health question. Gross harvest with unknown consumption answers
   nothing about pack survival; net answers it directly.
3. This is precisely how the fleet's most trusted power insight to date was
   produced: the gisebo-01/04 chemistry identification came from multi-day
   voltage slopes, not from any current measurement.

**Robustness: highest of all options for the question being asked, because the
integration is physical and the failure modes are already characterised. Ease:
zero firmware change — it is a backend computation over data every frame
already carries (TODO item 10).**

## Decision, per the weights

| Option | Compatible | Robustness (dominant) | Ease (tie-break) | Verdict |
|---|---|---|---|---|
| E. Battery-trend as energy-balance metric | yes, zero change | **highest** — physical integration, no aliasing, failure modes already measured | trivial (backend, item 10) | **ADOPT** |
| A1. 128-sample averaging | yes | unchanged (aliasing dominates) | trivial but pointless churn | reject |
| A2. INA219 continuous | yes (wiring) | destroys the budget it measures | — | reject |
| D. Micro-wake chunked PROD sampling | yes | measurement ↑ but **system ↓** (un-DEV-testable sleep path) | moderate–hard | reject; documented fallback |
| B. INA226/228/229 swap | **no** | accumulators are continuous-mode-only (~640 µA) — trap persists | — | reject, recorded as the wrong answer |
| C. LTC2942-class coulomb counter | **no** (new part) | highest for gross harvest | board revision | defer to item 11 BOM |

Concretely:

1. **`harvest_mah` is reclassified as an indicative diagnostic, not a
   measurement.** Its PROD error bar is ±40%-class per hour with varying sign
   (measured 2026-07-29); DEV carries the true profile via schema 2. This
   closes the "quantify the harvest error bar" half of item 15 — the answer is
   that the error is structural to point-sampling a sleeping device, not a
   calibration constant to be tuned out.
2. **The robust solar-input signal is the battery-voltage trend**, computed in
   the backend from data already transmitted: multi-day slope, temperature-
   corrected using the co-transmitted water temperature, exactly as done by
   hand for gisebo-01/04. Folded into item 10.
3. **If gross harvest ever becomes a requirement**, the answer is a
   LTC2942-class coulomb counter at a board revision (item 11), and explicitly
   NOT an INA228 swap.

## Sources

- TI SBOS448G (INA219): no accumulator; averaging table; quiescent figures —
  plus TI's product-family comparison noting INA219 has "no power accumulator".
- TI INA228 datasheet + E2E thread "INA228: accumulated energy and charge is
  wrong": accumulation registers invalid outside continuous mode; shutdown ≈
  5 µA stops accumulation; continuous ≈ 640 µA.
- Analog Devices LTC2941/LTC2942 datasheets: <100 µA quiescent, I2C gas
  gauge, integration through the sense resistor while the host sleeps;
  LTC2941 EOL status via the done.land coulomb-counter survey.
- done.land "Coulomb Counters" survey: LTC4150/2941/2944/INA228 comparison;
  the continuous-integration-vs-sampled-snapshot tradeoff.
- Hackaday SOL project (long-term solar intensity sensing): the micro-wake
  pattern with measured per-sample cost (53 ms @ 12 mA) and 6–7 µA sleep.
- Li-ion SOC literature (ScienceDirect / applied energy papers): plateau
  flatness ~3.9–3.5 V over 80–20% SOC; 50 mV ↔ 20–30% SOC in the flat region;
  trend/observer methods required in plateau regions.
- This repo's measurements: 2026-07-29 schema-2 profiles (the ±63%/−36%
  figures), the 2026-07-17 battery-voltage-tracks-temperature dev-note
  (+12.8 mV/°C, r = 0.93), and the 7–28 mAh/day harvest budget from
  solar_signal.h.
