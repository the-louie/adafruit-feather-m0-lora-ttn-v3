# Multi-sensor support (v4+) — analysis

Status: **analysis only. Not scheduled, no tasks filed.** Recorded so v3 does not make v4 harder.

Management wants up to three extra DS18B20 sensors in a future version — **box** (inside the enclosure), **air** (outside), **depth** (water at a second depth) — in some combination. All units will be identical once the design is settled; the variance is only during design. Not for v3.

**Sensor geometry, confirmed 2026-07-17:** the existing "surface" sensor sits at **0.5–1 m**; the depth sensor would start at **1–2 m** and **move to 3–6 m if the 1–2 m delta proves negligible**. **Surface drives the season; depth is product data only.**

That contingency is the single most important constraint in this document. **The placement is decided from the data the sensor itself produces**, so the encoding must serve both regimes without a reflash — a relocation is a site visit, and the wire format must not be the reason it becomes a firmware event too.

The question this document answers is narrow: **what must v3 do, or avoid, so that adding these later is cheap?**

## Summary of conclusions

1. **Pin-per-role, not a shared bus.** The pin *is* the sensor's identity. This is the only option that preserves the one-binary property the whole architecture rests on.
2. **Sampling extra sensors is free. Reporting them is not.** All DS18B20s on a bus convert in parallel, so N sensors cost the same 750 ms as one. Decouple sample cadence from report cadence.
3. **Temporal delta encoding (sample-to-sample) is not worth it.** It saves ~10 ms of airtime per uplink against a 30 s/day budget, and it breaks the length-based protocol detection the decoder depends on. Quantified below. Note this is a different proposal from point 6 — *spatial* delta between two sensors is worth doing, for the opposite reason.
4. **The real protocol work is encoding range, not compression.** `-10…+30 °C` is a *water* range. Swedish air reaches −25 °C and would be sentinel-clipped all winter.
5. **v3 needs four small changes**, all naming or guards. Nothing structural.
6. **Depth should be encoded as a delta from surface — for resolution, not compression — spanning −20.0…+4.9 °C at 0.1 °C/step.** The asymmetric range is physical: depth is colder than surface except under ice, where water's 4 °C maximum density caps the inversion. One encoding covers both the 1–2 m and 3–6 m placements, so the sensor can be relocated without a reflash.
7. **A depth sensor without pair calibration measures nothing.** Two uncalibrated DS18B20s have ±0.71 °C of delta error — wider than the stratification being measured. The lake solves this for free at turnover.

## Research: 1-Wire best practice

Confirmed from vendor behaviour and field reports rather than assumed:

- **Conversion is parallel.** `requestTemperatures()` issues a SKIP ROM convert to the whole bus, so every sensor converts simultaneously — [still 750 ms total regardless of sensor count](https://github.com/milesburton/Arduino-Temperature-Control-Library/blob/master/DallasTemperature.cpp). Reading back is sequential but trivial (~10 ms each).
- **Topology is the reliability factor, not sensor count.** Linear daisy-chain works; [star topology creates reflections that corrupt the bus](https://forums.raspberrypi.com/viewtopic.php?t=167896), and it is the most common cause of erratic multi-sensor setups. One reported case was fixed purely by re-cabling a star into a chain.
- **Practical limits are capacitance-driven**, roughly [10–20 sensors on a short bus](https://lastminuteengineers.com/multiple-ds18b20-arduino-tutorial/), but far fewer on long runs — [one field report hit its limit at 7 sensors over 60 m](https://forum.allaboutcircuits.com/threads/issue-connecting-20-ds18b20-sensors.180247/). 1-Wire is otherwise good for 100 m+ with a proper pull-up.
- **Splitting across multiple pins is the standard answer** for demanding cases — many sensors, long cables, or high reliability requirements.
- 4.7 kΩ pull-up per bus is mandatory; one per bus, not one per sensor.

## Weighed against our use case

The generic advice says "shared bus is fine for 4 sensors". Our constraints override it.

### Pin-per-role wins on role assignment, not on electrics

Four sensors on one bus is electrically trivial. The problem is **which sensor is which**.

On a shared bus, `getTempCByIndex(0)` returns devices in **ROM-address sort order** — an arbitrary property of the silicon. Adding a second sensor to the current bus would **silently reassign which device is "the water temperature"**, with no error and no symptom except wrong data. Fixing that means addressing by explicit 64-bit ROM ID, which means **a per-unit ROM table**.

That is disqualifying. The architecture is built on *one binary for every board*:

- the INA219 I2C probe exists so the variant is detected, not configured;
- latitude was pushed into the decoder specifically to keep firmware site-agnostic;
- `docs/generate-keys-from-feather-serial.md` proposes deriving even the LoRaWAN keys from silicon ID for the same reason.

A per-unit ROM table would be the first per-unit config in the system, and it would need maintaining every time a sensor is replaced in the field — at a post, at a lake.

**With pin-per-role the pin is the identity.** One `OneWire`/`DallasTemperature` instance per pin, each with exactly one device, each `getTempCByIndex(0)` unambiguous. No table, no config, no reassignment risk. Sensor replacement is plug-and-play.

Secondary benefits that matter here specifically:

- **Failure isolation.** A shorted cable in a lake kills its own bus, not all sensors. Water and depth sensors share a long submerged cable run; box and air do not. One bad splice should not blind the enclosure sensor.
- **Topology is automatically linear.** One device per bus cannot be a star. The single most common 1-Wire failure mode is designed out rather than documented against.
- **Auto-detection falls out for free**, and mirrors a pattern already in the design: probe each pin at boot, include what answers. That handles management's "combinations of the three are on the table" without a build flag — and once the design is nailed down, the same probe catches a sensor that has failed or come loose.

Cost: 3 extra GPIO. The Feather M0 has A0, A1, A3, A4, A5, 5, 9, 10, 12 free after LMIC (8, 4, 3, 6), strap (11), LED (13), OneWire (A2), VBAT (A7), and I2C. Not scarce.

**The one real cost** is that a shared bus could put water+depth on a single cable run with 3 conductors; pin-per-role needs 4. Trivial against the rest.

### Sampling is free; reporting is not

Because conversion is parallel, **four sensors cost the same 750 ms as one**. There is no energy or time argument for sampling aux sensors less often. Read everything, every wake.

What costs is **payload bytes**. So the cadence question is not "how often do we sample" but "how often do we report", and it should be answered per sensor by what the data is *for*:

| sensor | role | suggested reporting |
|---|---|---|
| surface (0.5–1 m) | product data, **drives season** | 6 samples/message, as today |
| depth (1–2 m) | product data — stratification | 6 delta samples/message |
| air | context | 1 sample (latest) |
| box | diagnostic — does the enclosure cook in July? | 1 sample (latest) |

v3 currently conflates sample cadence with report cadence — 6 samples, then an uplink. Keeping that conflation out of the *core* is one of the v3 notes below.

### Temporal delta encoding: analysed and not recommended

Management specifically asked for delta packing to fit N values in fewer than N bytes. Read as *sample-to-sample deltas within one sensor's history*, the numbers do not support it. (Read as *spatial delta between two sensors*, it is worth doing — see the depth section below. The two proposals share a name and nothing else.)

**The byte budget is not under pressure.** Maximal config, everything at 6 samples:

```
3  header (interval + battery/counter)
6  water         6  depth        6  air         6  box
6  solar fields  1  sensor-present bitmask
= 34 bytes
```

DR0 (SF12) allows **51 bytes** — and that is the pessimistic floor; the device runs at DR5/SF7 where the limit is 222. The maximal config fits with 17 bytes of headroom at the *worst* data rate. With the per-sensor cadence above it is **24 bytes**.

**The airtime saving is negligible.** Computed against the real observed figure (`consumed_airtime: 0.056576s` for the 9-byte payload in the 2026-07-16 capture, which the standard SF7/BW125/CR4-5 formula reproduces exactly):

| payload | frame | airtime |
|---|---|---|
| 9 B (today) | 22 B | 56.5 ms |
| 34 B (maximal, uncompressed) | 47 B | 92.4 ms |
| ~24 B (delta-packed) | 37 B | 82.0 ms |

Delta encoding buys **~10 ms per uplink**. At 48 uplinks/day that is **0.5 s/day against a 30 s/day fair-use budget**, and about **0.02 mAh/day against 7–28 mAh/day consumption** — under 0.2%, in a system where ~290 µA of quiescent draw already dominates everything.

**And it costs three things that matter:**

1. **It breaks length-based protocol detection.** The decoder identifies the payload shape *by length* — 8 = v5, 9 = v6/v7 primary, 15 = v7 solar. Variable-length delta encoding destroys that discriminator, and it is the mechanism the whole multi-version fleet strategy relies on.
2. **It clips on air.** Water deltas are tiny — the real capture shows 30-minute deltas of 0–2 LSB (`145,143,141,139,137,136`). Air is not water: it swings ±10 °C daily, so a 6-hour winter interval could carry a 15 °C step = 75 LSB, needing 8 bits anyway. A delta width sized for air saves nothing; one sized for water needs an escape hatch, which is variable-length again.
3. **It cannot be debugged by eye.** Given the project's history — a decoder that was not the deployed decoder, vectors that never ran, a reboot flag that never fired — reducing the payload's legibility to save 0.5 s/day is a bad trade.

**Where it would pay:** if a future config needs >51 bytes and coverage forces DR0. Nothing on the table approaches that. Revisit only if the sample count per message grows substantially.

There is a genuinely elegant observation worth recording in case this is revisited: **the interval already normalises water deltas.** The season machine lengthens the interval precisely when the tank changes slowly, so delta-per-interval is roughly season-invariant by design, and byte 0 tells the decoder which width to expect. That makes interval-keyed variable-width deltas *tractable* for water. It does not make them worth it.

### Depth: the delta is the measurement

**Surface drives the season. Depth is product data only.** The season machine is therefore *unaffected* by v4 — the existing sensor keeps its job, and no new input touches the seasonal baseline. That is the single most useful thing about this clarification: it means v4 adds sensors without reopening the interval policy at all.

But the geometry changes what the depth sensor is *for*. Surface at 0.5–1 m is already sub-skin, and depth at 1–2 m is only 0.5–1.5 m below it. In a small Swedish lake the summer thermocline sits well beneath both, so **both sensors live in the epilimnion** — the wind-mixed upper layer — and read nearly the same temperature for most of the year. At spring and autumn turnover they are identical by definition.

So depth-as-an-absolute is largely a redundant copy of surface. **What carries information is the difference**: a shallow diurnal thermocline on a still sunny afternoon, or inverse stratification under winter ice. That is the quantity worth spending bytes on.

**And the current encoding is far too coarse for it.** The payload quantises to 0.2 °C, while the DS18B20's native 12-bit resolution is 0.0625 °C — the protocol discards **3.2×** of the sensor's precision. Against a realistic 0.5–2 °C stratification that leaves only 2–10 distinguishable levels. The instrument can see the signal; the wire format cannot carry it.

### The span must survive relocation

**A ±7.9 °C delta at 0.0625 °C/step was the obvious answer and it is wrong.** It works at 1–2 m and clips catastrophically at 3–6 m — where the sensor may sit *below the summer thermocline*, in the hypolimnion, while the surface is 17 °C warmer:

| placement | condition | surface | depth | delta |
|---|---|---|---|---|
| 1–2 m | summer | 22.0 | 21.0 | **−1.0** |
| 1–2 m | under ice | 1.5 | 3.0 | **+1.5** |
| 3–6 m | summer, below thermocline | 22.0 | 5.0 | **−17.0** |
| 3–6 m | summer, *at* thermocline | 22.0 | 12.0 | **−10.0** |
| 3–6 m | under ice | 1.5 | 4.0 | **+2.5** |
| either | turnover | 8.0 | 8.0 | **0.0** |

Physical range: **−17.0 … +2.5 °C**, and strongly asymmetric — depth is colder than surface in every condition except under ice, where water's 4 °C maximum density caps the inversion at about +4.

**The encoding that survives both placements:**

| | |
|---|---|
| raw 0…249 | **−20.0 … +4.9 °C at 0.1 °C/step** |
| raw 250 / 251 / 252 | null / clipped-low / clipped-high — **matches the existing sentinel convention** |

Still 8 bits, same byte count as an absolute reading, **2× the resolution of today's 0.2 °C**, with 3 °C of low headroom and 2.4 °C of high. Fixed-width, so unlike temporal deltas it does not break length-based protocol detection. And it is bounded by physics rather than by hope, so unlike air it cannot clip in normal operation.

**0.1 °C is the right step, and 0.0625 °C was false precision.** After the turnover offset is removed the pair is good to roughly ±0.2–0.3 °C — the DS18B20's error *curve shape* differs per part, so calibrating at 4 °C (turnover) and measuring at 20 °C (summer stratification) does not fully transfer. Encoding finer than the accuracy would have been decoration.

**Why delta still beats absolute at 3–6 m**, where depth is genuinely independent data rather than a near-copy of surface: it is still 2× the resolution for the same byte, it directly encodes the quantity of interest, and absolute depth is recoverable as `surface + delta`. The cost is that recovered absolute depth carries both sensors' error rather than one — acceptable, because the stratification is the science and the absolute is the by-product.

**The decisive argument is the relocation itself.** The 1–2 m deployment exists to answer "is there a delta here?", and that question can only be answered with resolution fine enough to see a 0.5 °C signal — which today's 0.2 °C absolute encoding cannot (2 levels). So the fine-resolution delta is *required for the decision phase*, and the wide span means the same firmware keeps working after the sensor moves.

This is worth doing *even though the byte budget has room*, because it buys data quality rather than airtime.

### The accuracy trap: an uncalibrated pair measures nothing

A DS18B20 is specified at **±0.5 °C absolute**. Two of them differencing gives **±0.71 °C** of error (RSS) — **wider than the stratification being measured**. Encoding the delta at 0.0625 °C resolution while the pair carries 0.71 °C of systematic offset would produce beautifully precise nonsense.

The delta therefore needs **pair calibration**: the constant offset between the two specific sensors must be measured and removed.

**The lake does this for free, twice a year.** At spring and autumn turnover the water column is fully mixed and genuinely isothermal — so any delta the pair reports at turnover *is* the sensor offset. The backend can auto-calibrate by taking the median reported delta across a turnover window and subtracting it thereafter. No firmware change, no bench procedure, no field visit — and it re-calibrates annually, which also catches drift and sensor replacement.

This is backend work and costs nothing in v3 or v4 firmware. It should be recorded now because **the raw delta must stay on the wire uncalibrated** for it to be possible — the firmware must not "helpfully" apply an offset it cannot know.

### The real protocol work is range, not compression

`encodeTemperature()` maps `-10…+30 °C` to 0…200 at 0.2 °C/step, with 250/251/252 sentinels. **That is a water range.** A tank or lake lives inside it.

Air at this site does not. Swedish air reaches −25 °C, so an air sensor on the water encoding would report sentinel 251 ("too cold") for much of the winter — plausible-looking, useless, and exactly the class of silent wrongness this project has already been bitten by twice.

So v4 needs **per-sensor encodings**, not per-sensor compression:

| sensor | plausible range | note |
|---|---|---|
| surface | −10…+30 °C | unchanged; 0.2 °C/step; drives season |
| depth | −20.0…+4.9 °C **delta from surface** | 0.1 °C/step; sentinels 250/251/252 — see above |
| air | −40…+40 °C | 0.32 °C/step in one byte, or 0.2 °C at 400 steps = 9 bits |
| box | −40…+60 °C | can exceed air — a sealed dark box behind a south-facing panel in July |

Box deserves the wider top end specifically: the enclosure is sealed, dark, and mounted behind a panel in direct sun. Its whole diagnostic value is telling you when it cooks, which is exactly the reading a water-ranged encoder would throw away as "too warm".

## What v3 must do (all small)

None of this is structural. v3 is already close to right, largely because the item 1 fix and the `PowerPolicy` split happen to generalise well.

1. **Rename `encodeTemperature()` → `encodeWaterTemperature()`.** The −10…+30 range is water-specific and the name hides it. v4 then adds `encodeAirTemperature()` naturally, and nobody reuses the wrong range by accident. Pure naming; zero risk.
2. **Rename `lastTempC` → `surfaceTempC`.** Not `waterTempC` — v4 has *two* water sensors and only the 0.5–1 m one drives the season. When four temperatures exist, "the temperature" stops meaning anything, and the season machine must never be fed air, box, or depth. `surfaceTempC` names the sensor *and* the role, and it stays correct once depth arrives.
3. **Assert exactly one device on the A2 bus** (`sensors.getDeviceCount() == 1`). v3 keeps `getTempCByIndex(0)`, which is correct *only* while the bus has one device. The assert makes a mis-wired second sensor loud instead of silently reassigning which reading is "water". One line, and it is the guard that protects the v3→v4 transition.
4. **Keep the conversion window owned by the core, not the sensor read.** The item 1 fix already does this — it measures the window from `requestTemperatures()` and lets `PowerPolicy::onWake()` borrow part of it. That pattern generalises directly to "issue convert on all buses, wait once, read all". Do not let the 750 ms wait migrate back inside a per-sensor function.

## What v3 must avoid

- **Do not introduce per-unit configuration.** No ROM tables, no per-board sensor maps. It would be the first per-unit config in the system and it would undermine the probe, the key-derivation plan, and the decoder-side latitude decision all at once.
- **Do not harden "only the power policy appends payload bytes".** Aux sensors are not a power concern, so v4 needs a second contributor. A single `appendPayload` hook is fine now, but the core should treat the payload as **ordered self-describing sections** rather than "base + policy". The solar variant already establishes the pattern; keep it a pattern rather than a special case.
- **Do not hardcode the batch-buffer bounds.** `dataBuffer[6]` with a shift loop written as `for (int i = 5; i > 0; i--)` already hardcodes what `MAX_BATCH` is supposed to define. v4 wants `[sensor][sample]`; making the bounds derive from the constant now costs nothing and removes a rewrite later.
- **Do not spend the sensor-present bitmask byte early.** v4 wants it; v3 has no use for it and the status byte already has spare bits.

## Open questions for v4 (not now)

- **Depth placement should be a per-device decoder constant**, like `FIRMWARE_VERSION` and latitude. Firmware stays one-binary and depth-agnostic; the backend needs the depth to interpret the delta, and a relocation becomes a decoder edit rather than a reflash. This is the third instance of the same pattern — worth naming as a convention rather than rediscovering a fourth time.
- **A sensor at 3–6 m may sit *at* the thermocline, not below it** — and the thermocline deepens through the summer, so a fixed sensor would be crossed by it. That produces the most informative signal available and the most placement-sensitive one: metres of thermocline drift show up as several °C of delta swing. Worth knowing before anyone reads a wandering summer delta as a fault.
- **Is the −10…0 °C part of the water range dead, and is that a bug or a feature?** Liquid water cannot sit below 0 °C, so a quarter of the 200-step water encoding is physically unreachable. Which means sentinel 251 ("too cold") on a *water* sensor is not a temperature reading at all — it is **"the sensor is not in the water"**: pulled out, low water level, or a failed probe. That is real diagnostic value hiding in a range that looks wasted. If v8 reworks the encoding anyway, decide deliberately: reclaim the range for resolution, or keep it as the fault channel. Do not reclaim it by accident.

- **Which combination actually ships?** Box alone, box+air, all three. Auto-detect makes this a wiring decision rather than a firmware one — but the alarm story differs: a missing *depth* sensor is lost product data, a missing *box* sensor is lost diagnostics.
- **Does the sensor-present bitmask go in the payload or is absence inferred?** The INA219 probe's failure mode argues for explicit reporting — a loose sensor silently vanishing from the payload is the same silent-decommission shape, just less severe.
- **Protocol version.** v8. Reserve it; do not reuse v7.
- **Air temperature has an interesting side effect**: it makes the "cold battery illusion" reasoning testable. The season machine keys on water temperature partly because a cold pack reads low; with real air temperature on the wire, that correlation becomes measurable rather than assumed.
