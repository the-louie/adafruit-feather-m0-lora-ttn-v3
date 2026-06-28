# Solar variant — design

Status: **agreed, not implemented.** Captures the decisions from the design interview. Open hardware questions are listed at the end and are not settled.

A second deploy target: a post-mounted, true-south, vertically-mounted 5 V 0.15 W panel charging a 1S2P 18650 pack through an INA219, with a supercapacitor across the pack to cover the radio's 120 mA TX spike in the cold. The existing non-rechargeable build stays supported. The firmware splits three ways: a variant-agnostic core, the current temperature/battery policy, and a new solar policy.

## Architecture

**Three parts, one binary.**

- **Core** (variant-agnostic): sensor read, batch buffer, payload assembly, LMIC, TX, sleep. Owns `loop()`'s two-phase flow unchanged.
- **`PrimaryCellPolicy`**: today's algorithm, 6 V pack thresholds, 9-byte payload, FPorts 10/20.
- **`SolarPolicy`**: li-ion thresholds, INA219, 15-byte payload, FPorts 11/21.

The two policies sit behind a virtual `PowerPolicy` interface — `begin()`, `onWake()`, `decideInterval(tempC, vbat)`, `appendPayload(buf)`, `fport(runMode)`. The vtable cost is irrelevant on SAMD21. The season state machine (thresholds + 1 °C hysteresis) is **shared**, not duplicated: both policies use the same seasonal baseline and differ only in voltage bands and what they add on top.

**Variant selection is a runtime I2C probe** for the INA219 at 0x40 during `setup()`. Found → solar policy; absent → primary-cell policy. One firmware image for every board, consistent with the "compile once, flash everywhere" goal in `generate-keys-from-feather-serial.md`. The INA219 has no ID register, so the probe is an address ACK plus a config-register sanity read.

### The probe's failure mode — read this before touching the probe

A dead INA219, a loose wire, or a hung I2C bus makes a **solar board boot the primary-cell policy**. Those thresholds (5.0/4.3/3.5 V) are all above a full li-ion's 4.2 V, so every reading scores `voltage_offset = 3` and the device pins itself at interval index 10 — 7 days — permanently. A component fault silently decommissions the unit.

The FPort makes this observable: a board reporting on FPort 10 with a battery reading below ~4.5 V is definitionally misdetected, since no healthy 6 V primary pack sits that low. **The backend must alarm on that combination.** It costs nothing in firmware and it is the only thing standing between a loose connector and a unit that goes quiet for a year.

## Protocol versioning

**v6 is the current version. New work is v7.** Earlier numbering confusion does not change this — the deployed decoder's `version: 5` label is a hardcoded string, not a derived value, and is a bug (see below).

| Version | Shape | Where |
|---|---|---|
| v5 | 8 bytes: battery+seq, 6 temps. No interval byte. | `gisebo-04` — legacy, firmware source not in this repo |
| v6 | 9 bytes: interval, battery+**wake counter**, 6 temps. | `gisebo-01` — **current** |
| v7 | 9 bytes primary (battery+**uplink counter**) **and** 15 bytes solar. | New work |

**v7 covers both new shapes**, not just solar. Bytes 0–8 are byte-identical between the primary and solar variants; FPort and length distinguish them (10/20 → 9-byte primary, 11/21 → 15-byte solar).

### Why the primary variant needs the bump too

Changing the 4-bit field from a wake counter to an uplink counter leaves the layout **byte-identical** — same nine bytes, same positions, different meaning. **Nothing in the payload distinguishes v6 from v7.** A decoder reading a v6 wake counter as a v7 uplink counter sees jumps of 6 and reports dropped messages that never happened.

That is not a transition annoyance to be waited out; it is a silent semantic change on the wire, and it is the strongest argument for a version number.

### One decoder, one per-device constant

Decoders are set **per device** in the TTN console, and with two units — where a reflash is a site visit — provisioning already knows what firmware each unit runs. So the ambiguity the payload cannot resolve, configuration can:

```js
// Set per device to match the firmware flashed on THIS unit.
// 5 = 8-byte legacy | 6 = 9-byte wake-counter | 7 = 9-byte/15-byte uplink-counter
const FIRMWARE_VERSION = 7;
```

**Superseded 2026-07-17 by the export — do NOT combine.** This section argued for one decoder on the grounds that the live one branched on length and that two files would drift. Both premises were wrong, and `decoders/` now holds the evidence:

- There is **no application-level formatter**. Each device carries its own, and they **differ**. Neither branches on length — that was inferred from `decoded_payload` and was mistaken.
- **gisebo-01 is production and frozen**, so a combined decoder could never deploy there without reproducing its exact output schema byte-for-byte, `version: 5` bug and all.
- gisebo-01 is then **retired** — gisebo-05 replaces it. There is nothing to combine.

**Write a fresh decoder for gisebo-05, pinned at `FIRMWARE_VERSION = 7`.** The old two are frozen records in `decoders/`, not living code; the drift argument does not apply to files nobody maintains.

Everything except the counter semantics derives from length and FPort. The constant resolves only the one thing the bytes genuinely cannot.

### The `version: 5` bug

The deployed decoder hardcodes `version: 5` and reports it for `gisebo-01`'s 9-byte **v6** payloads. The label is a static string rather than a derived value. It is cosmetic in effect but actively misleading in practice — it caused a false diagnosis during planning that TTN was misdecoding v6 as v5. Derive it from length and `FIRMWARE_VERSION`.

## Uplink protocol

Bytes 0–8 are **byte-identical** across both variants and unchanged from today. The solar variant appends 6 bytes.

| Byte | Field | Encoding |
|---|---|---|
| 0 | Interval index | 0–10, as today |
| 1–2 | Battery offset + counter | 12-bit offset from 3000 mV; low nibble of byte 2 = 4-bit counter (**semantics changed, see below**) |
| 3–8 | Six temperatures | as today |
| 9 | Panel bus voltage | 30 mV/LSB, 0–7.65 V |
| 10 | Panel current | 0.5 mA/LSB, 0–127.5 mA |
| 11 | Sun-presence EWMA | 0–255 = 0.0–1.0 |
| 12–13 | Harvest accumulator | 1 mAh/LSB, cumulative since cold boot, 16-bit — **wraps; backend unwraps** |
| 14 | Status | bits 7–5 = boot counter (wraps at 8); bits 4–0 = flags |

FPorts: 10 = primary/PROD, 20 = primary/DEV, 11 = solar/PROD, 21 = solar/DEV. The variant is carried by the FPort, so no policy ID byte is needed.

Byte 14 flags (bits 4–0): bit0 cold boot (`.noinit` invalid), bit1 soft reset since last uplink, bit2 clock valid, bit3 solar bonus active, bit4 last TX timed out. The boot counter is 3 bits rather than 4 to make room for the clock-valid flag; it wraps at 8, which is ample since the backend only needs to see it increment.

The battery encoding (offset from 3000 mV, 12 bits, 1 mV/LSB) covers li-ion's 3.0–4.2 V without change — it just uses the bottom third of the range. Keep it; the decoder stays common.

**Why 15 bytes and not 12:** the solar policy will never be tested on-device — uplinks are the only instrument. A payload carrying only instantaneous V/I would show *what* the policy decided (byte 0) but not *why*, and the INA219 is read every wake while only one sample per message goes on the wire, so the backend can't reconstruct the EWMA from transmitted data. The predicted failure mode is multi-day hunting, and the EWMA is exactly the signal needed to diagnose it. Payload space is free here — DR0 allows 51 bytes.

### The 4-bit counter: what changed and why

`rebootDetected` in `ttn-decoder-v6.js:31` has **never worked**. `readAndBufferSensors()` increments `wakeCounter` before `loop()` evaluates the uplink condition, so the post-reboot fast-flush uplink carries sequence 1, never 0. In steady state with `batchTarget = 6` the sequence at uplink walks 1, 7, 13, 3, 9, 15, 5, 11 and never reaches 0 either. A TX-timeout retry storm uplinks on consecutive wakes and *can* hit 0, producing a false reboot. It is a false-negative machine that occasionally false-positives. (`doc/test-payloads.md` vector 1 asserts the fast-flush sends sequence 0 — the stale doc records the intended behavior the code never had.)

A free-running 4-bit counter cannot distinguish a reboot from a wraparound in principle, so this is a design fix, not an off-by-one fix:

- The 4 bits become an **uplink counter**, incremented once per successful TX. Consecutive uplinks then differ by exactly 1, so any gap is a dropped message — a far better use of the field, and the wire layout is untouched.
- **Reboot detection moves to the status byte**, solar variant only. The primary variant keeps drop detection and loses reboot detection; its 9-byte payload has no room and is not being widened.
- **The decoder must stop reporting `rebootDetected` on FPorts 10/20.** Leaving it in place ships a field that lies.

This applies to both variants, so the primary firmware changes too — semantics only, layout identical. Deployed units need a reflash to switch from wake counter to uplink counter.

## Solar policy

**Interval:** `season_base` (shared, from water temperature) then, **only if `voltage_offset == 0` AND the solar bonus is latched on** (EWMA > 0.55 engages, < 0.45 releases), subtract a **fixed 2 steps**. Clamp to [2, 10]. Decided 2026-07-17.

| season | base | with bonus |
|---|---|---|
| Summer | 4 (30 min) | **2 (5 min)** — the floor |
| Fall/Spring | 5 (60 min) | 3 (15 min) |
| Winter | 7 (6 h) | 5 (60 min) |

**Threshold: engage at EWMA > 0.55, disengage below 0.45** — a hysteresis band centred on the chosen 0.5, not a bare threshold.

A bare 0.5 **flaps**. The EWMA has a 24 h time constant fed a 24 h day/night cycle, so it ripples ±0.11 around its mean every single day. Steady-state swings:

| season | daylight | EWMA daily swing | vs 0.55/0.45 |
|---|---|---|---|
| Summer (clear) | 15.6 h | 0.533 → 0.756 | never below 0.45 → **stays ON** |
| Fall/Spring | 9.6 h | 0.286 → **0.522** | never reaches 0.55 → **stays OFF** |
| Winter | 6.0 h | 0.165 → 0.350 | → **stays OFF** |

Fall/Spring peaks at 0.522 *every afternoon* — so a bare 0.5 would engage the bonus daily and drop it nightly, dithering the interval 5↔3 through both shoulder seasons. The band separates the three regimes with no crossing at all, which is precisely the "summer only in practice" behaviour intended.

This mirrors the season machine's 1 °C hysteresis, for the same reason and against the same failure.

Expressed as a measurement rather than a season state, so a bright spring qualifies and an overcast summer week does not — that is why the EWMA is used instead of `season == Summer`.

Two gates. `voltage_offset == 0` means sun never shortens the interval on a struggling pack, so the signals cannot fight — and it is what makes the no-MPPT decision safe, since motorboating corrupts the bus voltage only while charging, i.e. exactly when the pack is not full and the bonus is off. The loop self-corrects: if shortening outruns harvest, the pack drains, the bonus disappears, and the interval returns to baseline.

**The threshold is provisional** — no field EWMA distribution exists. Byte 11 uplinks the raw EWMA so it can be tuned from real data.

**Floor is index 2 (5 min), batch stays 6** — 6× the summer sampling rate, uplink every 30 min, ~48 uplinks/day, well inside TTN's 30 s/day fair-use airtime. See Risks.

**Li-ion bands: 3.85 / 3.65 / 3.45 V** (healthy / low / critical), replacing 5.0 / 4.3 / 3.5. Healthy at 3.85 is roughly 60%+ SOC, where a solar-fed pack should normally live, so the bonus is usually available. Critical at 3.45 keeps margin above the Feather's ~3.4 V brownout. Note that VBAT is sampled at wake and before `LMIC_setTxData2()`, so it reads essentially open-circuit — cold sag under the 120 mA TX load never enters the measurement, and the supercap covers it in any case.

**Solar signal:** panel bus voltage is a clean day/night discriminator. At night the panel is dark, the Schottky blocks, and the bus sits near 0 V; whenever there is sun the bus reads either the charger's operating point (~4.5–5 V) or, once charge terminates, the panel's open-circuit voltage. So `sun_present = (bus_mV > 3000)`, and the EWMA of that is the fraction of recent time with usable sun.

This deliberately does **not** use current as the primary signal. The INA219 sits in the charging path and measures *harvested* current, not available sunlight: when the pack is full the MCP73831 terminates and input current collapses to quiescent, so in full summer sun with a full battery the current reads ~0. With the claimed surplus that is most of the summer. Current is still uplinked and accumulated for energy analysis — it is just not what the policy keys on.

**The window is time-based, not wake-based.** A window of N wakes is a window of N × interval seconds, and the interval is what the window controls: a sunny afternoon shortens the interval, which shrinks the window to a few hours that are still all daylight, so it never sees the night that should pull the average down — then night stretches it back. It hunts on a multi-day period. Take `dt` from the RTC (see Timekeeping) and decay against it:

```
alpha = 1 - exp(-dt / TAU)     // TAU = 86400 s
ewma += alpha * (sun_present - ewma)
```

`exp()` once per wake is free at this duty cycle. Do not substitute the linear approximation `alpha ≈ dt/TAU` — it errs ~13% at a 6 h interval, which is exactly the winter case.

**Day length normalizes the EWMA; it does not replace it.** The raw EWMA is roughly "fraction of recent time with usable sun", which conflates two things: how long the sun is up (season) and how often it's actually shining (weather, or a fouled panel). With a valid clock and a known latitude, expected daylight fraction is computable, and the ratio separates them:

```
clarity = ewma / expected_daylight_fraction
```

~1.0 means clear skies. Persistently low means overcast — **or snow, leaves, or shade on the panel**, which a vertical mount in July would otherwise hide behind a plausible-looking low EWMA. That is a fault class the design currently has no way to observe.

This costs **zero payload bytes**: the backend has the TTN uplink timestamp and the latitude constant, so it computes the expectation itself. Byte 11 stays the raw EWMA.

Without a valid clock the solar policy degrades to the raw EWMA against a fixed threshold. Season is unaffected either way — it is temperature-based and needs no clock at all.

**Season stays on water temperature.** Day length was considered as a season signal and rejected. The season machine exists to sample less when the tank changes slowly (master-plan), and water temperature reads that directly, where day length is an astronomical proxy for weather which is a proxy for the tank — two steps removed, and wrong during an unseasonable warm spell. Day length's advantages are drift-freedom and predictability: the flapping it would prevent is already prevented by the 1 °C hysteresis, and the energy efficiency it would optimize is worth under 4% (see Risks). Each signal is used for what it is good at: temperature for season, day length for solar expectation. The season machine therefore stays shared between both policies, and the primary variant is unaffected.

**INA219 is read every wake, inside the Dallas conversion window.** The DS18B20 needs 750 ms and the MCU is already spending it. Order: `requestTemperatures()`, wake the INA219, trigger a conversion, read, power it down, then `LowPower.idle()` for the remainder (PROD) or run the `os_runloop_once()` loop (DEV). This needs no wake-from-idle interrupt and adds no awake time. Sampling every wake — not once per uplink — is what makes a clockless day/night estimate work at all.

**Calibration:** use `setCalibration_16V_400mA()` for a 0.1 mA LSB. The breakout's 32 V/2 A default gives a 0.8 mA LSB, which is ~4% resolution against a 30 mA panel.

## Timekeeping

The SAMD21 has an RTC that keeps counting through standby, and the Feather M0 carries an external 32.768 kHz crystal. The Arduino SAMD variant for this board does not define `CRYSTALLESS`, so `RTCZero` sources GCLK2 from that crystal rather than the ULP oscillator: ~20–50 ppm, not the wild drift of the internal RC.

**This is already in use.** `ArduinoLowPower` on SAMD is a thin wrapper over `RTCZero` — it holds its own instance, sets an alarm epoch, and enters standby. So `LowPower.deepSleep()` is already crystal-backed and already accurate. Adopting `RTCZero` explicitly does not unlock *sleeping*; it unlocks **reading** the time. (`millis()` genuinely does not advance through `deepSleep` — it is clocked from the main clock, which stops. That is a fact about `millis()`, not about the hardware.)

**Ownership:** `ArduinoLowPower` keeps the sleep and idle paths exactly as today; a separate `RTCZero` instance is added for `getEpoch()` reads. Both wrap the same peripheral, so `begin()` is called twice and alarm configuration can collide — this is the seam to watch, and the reason the proven sleep path is not being restructured.

**Wall clock** is seeded once via `DeviceTimeReq` on the first uplink after join. With this crystal, one acquisition holds for months (~4 s/day drift), so it is a one-shot rather than an ongoing downlink dependency. Requirements and traps:

- Needs `LMIC_ENABLE_DeviceTimeReq` in `lmic_project_config.h` — the same file that must already define `CFG_eu868`, and which is not settable from the sketch.
- Returns GPS epoch: convert with the 315964800 offset plus leap seconds (18 as of writing).
- Only arrives in an RX window after an uplink, and can simply not land.

**The RTC does not survive `NVIC_SystemReset()`.** SAMD21 has no backup domain, so the PROD join-failure path at `adafruit-feather-m0-lora-ttn-2.ino:404` resets the RTC along with everything else. Read the epoch after the 15-minute sleep, stash it in `.noinit`, reset, restore on boot.

**Latitude is a single hardcoded constant.** Day length is insensitive to small errors — a degree or two is minutes — but this does break the one-binary goal for any unit deployed far from the assumed site. It fails silently if that ever happens.

**No clock is a supported state.** Until `DeviceTimeReq` lands (or forever, in poor downlink coverage), the solar policy runs on the raw EWMA and the clock-valid flag reports the degraded state. Season is temperature-based regardless, so nothing else is affected.

## Persistent state

State lives in a `.noinit` struct guarded by a magic word and a layout version. A soft reset does not physically clear SRAM — only the C runtime zeroes `.bss` — so `__attribute__((section(".noinit")))` survives the `NVIC_SystemReset()` in the PROD join-failure path at zero flash cost, fully consistent with the no-FlashStorage rule. Magic or version mismatch → treat as cold boot, reinitialize, and set the cold-boot flag.

Contents: season state, `currentIntervalIndex`, `lastTempC`, uplink counter, boot counter, RTC epoch (saved before the join-failure reset), sun-presence EWMA, harvest accumulator.

Note there is no hand-rolled elapsed-time accumulator: an earlier draft summed `sleepIntervalSeconds` to reconstruct elapsed time, which is strictly worse than `rtc.getEpoch()` — it misses awake time and cannot survive a reset. The RTC does the job properly.

The layout version matters: a firmware upgrade that changes the struct must not read the old layout as valid, or the device resumes from garbage — strictly worse than a cold boot. Bump it on any field change.

Without this, every join-failure reset restarts at Summer/index 2 and re-walks the season machine one step per uplink, with solar history empty — in winter, precisely when joins fail most and the wrong policy costs most.

## Risks and things we chose to accept

**Subzero charging.** Accepted: cells are consumable on a 5–10 year replacement cycle. The rationale holds better than it first appears — plating is strongly rate-dependent, and the charge rate here is panel-limited to ~30 mA into ~6000 mAh, about C/200. The MCP73831 will ask for 100 mA and never get it. C/200 into a cold cell is far gentler than the C/2-and-up regimes the "never charge below freezing" rule addresses. Consequence: **replacement timing becomes a telemetry problem.** The supercap deliberately hides the TX sag, so pack internal resistance can't be inferred from a 50 ms transmit; the signal is resting voltage trending down over months while harvest stays normal, which is a backend inference. The solar payload fields are load-bearing for maintenance, not just diagnostics.

**No MPPT — decided 2026-07-17.** The panel feeds the Feather's USB pin through a Schottky and the INA219, charging via the onboard MCP73831. No external charger, no input-voltage regulation. The topology stands as originally designed.

The MCP73831 is set to 100 mA and the panel supplies ~30 mA, so it will drag the panel off its knee until it browns out, releases, recovers, and restarts — motorboating, costing perhaps 30–50% of harvest. That was accepted for four reasons:

- **There is no energy case.** With ~10× claimed summer surplus and 500 days of dark-spell reserve, halved harvest still leaves ~5× margin. This was never an energy decision.
- **The healthy-battery gate already covers the signal risk.** Motorboating only occurs while *actively charging*; a full pack terminates the charger, the panel goes unloaded, and the bus sits cleanly at Voc. So bus-voltage corruption happens precisely when the pack is not full — and the solar bonus is gated on `voltage_offset == 0`, a near-full pack. **The corrupted EWMA and the decision that consumes it never overlap.** The gate was chosen to stop sun and battery signals fighting; it happens to cover this too.
- **The INA219's own averaging is a free mitigation.** 128 samples takes ~68 ms and fits inside the 750 ms Dallas window with room to spare, averaging across the oscillation instead of point-sampling it. A single instantaneous read per wake would otherwise catch the cycle mid-collapse or mid-recovery at random, and the harvest accumulator would integrate coin flips — biased in an unpredictable direction, not merely noisy.
- **An external charger is not a drop-in.** It would mean `panel → INA219 → charger → BAT pin`, bypassing the onboard charger entirely, plus two chargers on one pack whenever USB is plugged in DEV. Real complexity on a post at a lake, bought against an estimate that was never measured.

Costs accepted with this decision, stated so they are not a surprise:

- **The harvest accumulator carries an error bar** until S07-05/06 quantify it. That weakens the pack-health trend (the only replacement-planning signal, since the supercap hides TX sag), which means replacing cells on a calendar rather than on evidence — mostly costing the "or more often if needed" half of the consumable policy.
- **The fouling alarm must gate on a full pack.** Clarity would otherwise read low during charging periods and false-positive.
- **There is no hardware fallback.** If the motorboat period turns out much longer than the ~68 ms averaging window, the remedies are longer averaging, multiple reads per wake, or a wider error bar — not a charger. S07-05 measures the frequency; that is the task that could reopen this.

**The floor rests on an unmeasured number.** The figures in the brief imply ~0.075 mAh per wake; a model of the wake (750 ms conversion plus an occasional 3 s TX) suggests closer to 0.002 mAh. At 5-minute sampling that is 28 mAh/day versus 7.6 mAh/day — the difference between eating most of the harvest and not noticing. The harvest accumulator will settle this from field data within a few months. Until then, index 2 is defensible mainly because the healthy-battery gate self-corrects if it turns out to be net-negative; expect hunting on a multi-day period if it is.

**Quiescent draw dominates everything.** From the brief's own figures, a 12× interval change (30 min → 6 h) moves consumption 31%, which backs out to ~6.9 mAh/day (~290 µA) drawn regardless of wake count — consistent with a stock Feather M0, where the regulator, charger, and USB circuitry leak ~300 µA. Accepted on the grounds that solar makes it affordable. **That reasoning does not extend to the primary variant**, where ~7.2 mAh/day means roughly a year on 4×AA and nearer seven months on 2× CR123A, and where the interval ladder saves under 4%. If multi-year life on primaries is ever a requirement, the answer is quiescent-current surgery, not interval tuning. Worth naming plainly: the temperature policy is not really an energy strategy — it is a chemistry-appropriate threshold set plus a proven, deployed sampling cadence. Both are good reasons to keep it.

## Open — check before building anything

**`LowPower.idle(750)` may be returning early, and if so production temperature data has been wrong all along.** `ArduinoLowPower`'s alarm has one-second granularity — `setAlarmIn` divides milliseconds by 1000 — so `idle(750)` computes a `+0` second alarm, which either returns immediately or sets an alarm in the current second that may already have passed. If it returns early the DS18B20 has not finished converting and returns its 85 °C power-on default, which `encodeTemperature()` maps to the "too warm" sentinel, 252.

This is PROD-only: the DEV path spends the 750 ms in an `os_runloop_once()` loop instead (`adafruit-feather-m0-lora-ttn-2.ino:184-191`), so DEV would never show it. That asymmetry is exactly where this class of bug hides, and it was introduced deliberately for USB stability (`docs/dev-notes/20260309-1700_usb-serial-stability-lowpower-idle-by-runmode.md`) with no reason to suspect a side effect.

**The test costs nothing and needs no bench:** look for 252 sentinels in FPort 10 history that FPort 20 never produces. If clean, drop this. If not, it outranks the solar variant entirely.

This is a hypothesis from recalled library internals, not a confirmed defect.

## Open hardware questions — not settled

- **Supercap voltage rating.** Across a 1S pack it sits on a 3.0–4.2 V rail; standard supercaps are 2.7 V rated. Needs a 5.5 V module (two in series, half the capacitance) or explicit part selection. Its leakage (µA to tens of µA) counts against the sleep budget alongside the 15 µA INA219. Part not chosen.
- **Over-discharge protection.** The Feather browns out near 3.4 V but keeps leaking tens of µA afterward, which walks a li-ion cell down to destruction over a long dark spell. Firmware cannot help once it is off. Protected cells or a PCM. Unresolved.
- **Serial and solar are mutually exclusive — DEV mode and solar are not.** USB puts 5 V on the same pin the panel feeds, so the Schottky blocks the panel and the INA219 reads ~0 mA on a 5 V bus. But **DEV mode is set by the strap (pin 11), not by USB presence**: strap DEV and leave USB unplugged and you get FPort 21, the busy-wait sleep path, no serial (the `while (!Serial)` wait simply times out) — and the panel feeding the USB pin normally, so the INA219 sees real solar. That is exactly what S07-04 does. The thing you cannot have is serial logs *while* observing solar; telemetry goes over the air instead.
