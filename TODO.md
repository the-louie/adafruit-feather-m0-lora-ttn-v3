# TODO

Work items. Each is broken into 1–2 hour tasks under `docs/sprints/sprint-NN/`.
Design decisions behind items 4–9 and 11 are recorded in `docs/solar-variant-design.md`.

---

## 1. `LowPower.idle(750)` does not wait — PROD temperature data is corrupt

**Status:** Not started — **highest priority; blocks trusting any PROD data, historical or future**
**Complexity:** Low to fix, unknown to remediate
**Estimated time:** 6–9 h (investigation + fix + dev-note)
**Sprint:** 01 (investigate), 02 (fix)

### Problem

**Confirmed from library source**, not a hypothesis. `ArduinoLowPower::setAlarmIn()`:

```c
void ArduinoLowPowerClass::setAlarmIn(uint32_t millis) {
	if (!rtc.isConfigured()) { attachInterruptWakeup(RTC_ALARM_WAKEUP, NULL, (irq_mode)0); }
	uint32_t now = rtc.getEpoch();
	rtc.setAlarmEpoch(now + millis/1000);      // 750/1000 == 0
	rtc.enableAlarm(rtc.MATCH_YYMMDDHHMMSS);
}
```

Integer division, no sub-second handling. `LowPower.idle(750)` at `adafruit-feather-m0-lora-ttn-2.ino:184` therefore sets the alarm to the **current second** and `__WFI()`s. `idle()` clears `SLEEPDEEP`, sets `PM->SLEEP.reg = 2` (IDLE2), and — unlike `sleep()` — does not disable SysTick.

Two possible outcomes, both bad, distinguished only by whether SysTick is gated in IDLE2:

- **Returns after ~1 ms** (SysTick still ticking). `requestTemperatures()` issues the convert; the read ~1 ms later returns the DS18B20 scratchpad, which still holds **the previous conversion's result** — completed comfortably during the last deep sleep. PROD therefore reports real temperatures **lagged by exactly one wake interval**. Plausible-looking, invisible on a slow tank, and it silently voids byte 0's extrapolation contract. First read after boot returns the sensor's 85 °C power-on default → `encodeTemperature()` → sentinel **252**.
- **Blocks until the alarm matches again** (SysTick gated; USB detached in PROD, MCCI LMIC polls DIO rather than using interrupts, so nothing else wakes it). PROD never transmits at all.

PROD-only: the DEV path spends the 750 ms in an `os_runloop_once()` loop (`:186-191`), so bench testing could never reproduce it. That branch was introduced for USB serial stability (`docs/dev-notes/20260309-1700_usb-serial-stability-lowpower-idle-by-runmode.md`) — the USB symptom described there ("suspending the CPU and causing the serial connection to reset on every measurement") was `idle(750)` gating the APB clock. The symptom was diagnosed and worked around for DEV; the truncation underneath was never suspected.

### Branch identified — 2026-07-16, from `docs/dev-notes/real-world-data__20260716.json`

**The early-return branch is confirmed. `idle(750)` does not hang.** Both deployed units transmit continuously (`gisebo-04` f_cnt 709→808, `gisebo-01` f_cnt 376→391), so the blocking branch is ruled out. **The one-interval temperature lag is therefore live on every PROD unit running v6.**

The two checks originally proposed here are both **impossible** against this data and must not be planned:

- *"252 in slot 0 of a fast-flush uplink"* — requires a reboot. There are **no f_cnt resets** in the window, so no post-boot uplink was captured.
- *"252s on FPort 10 that FPort 20 never shows"* — requires a DEV unit. **There are no FPort 20 uplinks at all**; nothing is deployed in DEV.

Confirming the lag from telemetry alone needs an **independent temperature reference** (a nearby station, or a second sensor at a known depth) or a **captured reboot**. Neither exists today. Options: instrument the backend to alarm on 252-in-slot-0 and wait for the next natural reboot, or accept the source-level proof and fix it regardless. **Recommend the latter** — the mechanism is certain, and the fix is cheap.

### Solution

The wait must actually wait. Options, in order of preference:

**Decided 2026-07-17: option 1, `delay(750)`.**

1. **`delay(750)` — chosen.** Safe here specifically: the radio is idle during sensor conversion, so the no-`delay()`-near-the-radio rule (master-plan, domain-knowledge) does not apply. Costs ~750 ms of run-mode current per wake, and leaves 682 ms of slack around the INA219's ~68 ms averaging. Add a comment saying why `delay()` is permitted, or the next reader will "fix" it back.
2. `idle()` in a loop against `rtc.getEpoch()` — awkward at second granularity for a 750 ms wait, and it re-enters the library whose truncation caused this defect.
3. Drop the DS18B20 to 9-bit (94 ms conversion, 0.5 °C steps) — **rejected**. It would leave only ~26 ms around the INA219's ~68 ms averaging, coupling sensor resolution to averaging so neither can be tuned alone. That matters because *lengthening* the averaging is exactly S07-05's remedy if motorboating turns out slower than 68 ms. Its only advantage is power, and that argument rests on the same unmeasured per-wake figure that already undermines the index-2 floor (item 11) — while quiescent draw (~290 µA) likely dominates run-mode cost anyway.

If S06-05's per-wake measurement later shows run-mode cost actually matters, revisit — but revisit with the INA219 coupling in view, not as a free swap.

**While here: average the VBAT ADC.** `getBatteryVoltage()` (`:26-30`) takes two dummy reads then **one** real sample. A single sample carries ~±19 mV of noise, which is what makes the voltage bands dither every wake (item 8). Averaging 16 samples costs microseconds inside a wake that now spends 750 ms deliberately, and cuts noise 4×. It attacks the dither at source; item 8's hysteresis absorbs the remainder. Do both.

### Collision with item 8 — the INA219 shares this window

Item 8 reads the INA219 **inside** the Dallas conversion window, with 128-sample averaging taking ~68 ms. A naive `delay(750)` after the INA219 read makes the wake 818 ms, not 750. The window must be measured from `requestTemperatures()`, not from the end of the INA219 read:

```c
uint32_t convStart = millis();
sensors.requestTemperatures();
policy->onWake();                       // INA219 wake + ~68 ms convert + read + power-down
uint32_t elapsed = millis() - convStart;
if (elapsed < DALLAS_CONV_MS) delay(DALLAS_CONV_MS - elapsed);   // PROD
```

DEV keeps its `os_runloop_once()` loop, with the same elapsed-time basis.

**This constrains option 3.** At 9-bit resolution the window is 94 ms and the INA219 needs ~68 ms of it — it fits, but with 26 ms of margin and no room if the averaging is ever lengthened (which is exactly the remedy S07-05 would reach for if motorboating is slower than expected). Option 1 (`delay(750)`) leaves 682 ms of slack. If option 3 is chosen, the INA219 averaging and the DS18B20 resolution become coupled, and neither can be tuned alone.

### Verification

- Compile-only at authoring time; hardware is ordered but arrives later than sprint 03, so the fix ships unverified and S06-02 verifies retroactively.
- Do **not** plan to verify on a DEV-strapped unit. The bug's entire character is that it does not exist in DEV — S06-01 brings up a **PROD-strapped** unit specifically for this.
- S06-13 reproduces the original defect on the bench with pre-fix firmware — the 252-in-slot-0 signature the field data could never show, because no reboot was captured. That either confirms or overturns this whole diagnosis.
- Add the backend alarm (item 10) *before* the fix ships, so the pre-fix baseline is captured from the field too.

### Notes

**Historical impact — dated 2026-07-17 from git, see `docs/dev-notes/20260717-1230_dating-the-lag-and-clearing-v5.md`:**

The defect was introduced by **`aad7bca`, 2026-03-09 12:03**, which replaced a working `delay(750)` with `LowPower.idle(750)` as a power optimisation. **The fix (S02-01) is that commit reverted.**

**V5 is clear.** The 8-byte payload predates `aad7bca`, so V5-era firmware uses `delay(750)` and is **not** lagged. `gisebo-04`'s history is trustworthy and needs no caveat — which also removes the data-integrity half of item 14.

**gisebo-01 is lagged**, but the start date is unprovable: there is a 21-minute window (11:42–12:03) where 9-byte firmware still had `delay(750)`, and nothing in telemetry distinguishes lagged from unlagged without an independent reference. Treat all of its 9-byte history as 30 minutes late.

Retroactive correction is possible in principle: shift each v6 PROD series back one interval. Byte 0 records the interval, so the shift is computable per message — but only for v6 units, and only where the interval was stable across the shift. For V5 units there is no interval byte at all, so correction depends on the assumed 5-minute cadence.

---

## 2. The fast-flush fires every 16 wakes, and `rebootDetected` has never worked

**Status:** Not started — **confirmed from production data**
**Complexity:** Low — but touches deployed units
**Estimated time:** 4–5 h
**Sprint:** 02

### Problem — confirmed 2026-07-16 from `docs/dev-notes/real-world-data__20260716.json`

Two defects in one place, both proven from 107 production uplinks across both deployed units.

**`wakeCounter == 1` is not "the first uplink after join".** It is a 4-bit counter, so it wraps to 1 **every 16 wakes** and re-triggers the fast-flush — with a partial batch. Observed sequence/batch-size pairs, both devices, no exceptions:

| seq | samples | gisebo-04 | gisebo-01 |
|---|---|---|---|
| 1 | **4** | 31 | 5 |
| 7 | 6 | 32 | 5 |
| 13 | 6 | 29 | 5 |

Simulating `loop()` reproduces this exactly: TX at wake 7 (`ramCount` 6), TX at wake 13 (`ramCount` 6), then `wakeCounter` wraps to 1 at wake 17 with only `ramCount` 4 → premature uplink, two slots wasted as `250`. Period 16 wakes, forever. **A third of every uplink ever sent** carries a short batch and two dead bytes. It accounts for all 31 of gisebo-04's `250`-in-slots-4-and-5 occurrences.

Not a data-loss bug — samples are contiguous across the short batch — but it wastes airtime and energy, makes the uplink cadence irregular, and means the "FAST-FLUSH" comment at `:430-433` describes behaviour the code does not have.

**`rebootDetected` is dead.** `ttn-decoder-v6.js:31` computes `rebootDetected = (sequence === 0)`. Across 107 uplinks the sequence takes exactly three values — **1, 7, 13** — and is never 0. `readAndBufferSensors()` increments `wakeCounter` *before* `loop()` evaluates the uplink condition (`:206`, then `:433`), so the post-reboot uplink carries 1, and the steady-state cycle never reaches 0 either.

Fixing the off-by-one does not work: moving the increment after the uplink decision makes the boot uplink send 0 correctly, but then the cycle becomes 0, 6, 12 — which *includes* 0, so every third uplink would report a false reboot. A free-running 4-bit counter cannot distinguish a reboot from a wraparound in principle.

### Solution

- **Give the fast-flush its own flag.** A `bool firstUplinkAfterJoin` set once on `EV_JOINED` and cleared after the first TX. This is the actual intent, and it removes the every-16-wakes misfire.
- The 4 bits become an **uplink counter**, incremented once per *successful* TX (in the `if (txComplete)` block of `transmitBatchAndWait()`, alongside `ramCount = 0`). Consecutive uplinks then differ by exactly 1, so any gap is a dropped message. Wire layout unchanged.
- **Remove `rebootDetected` from the decoder for FPorts 10/20**; rename the output `uplink_counter` so the changed semantics are visible rather than silently reinterpreted.
- Reboot detection returns on the solar variant only, via the status byte (item 8).

### Verification

- Host tests: counter increments once per successful TX, not per wake; does not advance on a failed TX.
- Host tests: simulate 40 wakes and assert every uplink carries a full 6-sample batch except the genuine first-after-join. The current code fails this test — it is the regression test for this item.
- Post-deployment: sequence values on FPort 10 must stop clustering on {1, 7, 13} and start incrementing by 1.

### Notes — this is a protocol version bump

Changing the 4 bits from a wake counter to an uplink counter leaves the layout **byte-identical**. Same nine bytes, same positions, different meaning. **Nothing in the payload distinguishes v6 from v7**, and a decoder reading a v6 wake counter as a v7 uplink counter sees jumps of 6 and reports dropped messages that never happened.

So this is **v7**, not a silent semantic change to v6. See `docs/solar-variant-design.md` § Protocol versioning. The decoder resolves it with a per-device `FIRMWARE_VERSION` constant — decoders are set per device in TTN, and with two units where a reflash is a site visit, provisioning already knows what is flashed.

### No fleet reflash — superseded 2026-07-17

An earlier draft said "deployed units need a reflash" and sequenced the decoder change against it. **That is wrong and the concern evaporates.**

- **gisebo-01 is PRODUCTION and frozen.** It feeds a webhook into influx/grafana; its decoder's output *is* the influx schema, so renaming `sequence` → `uplink_counter` there would break grafana. Its decoder stays as-is, defects and all.
- **gisebo-04 is not being touched.**
- **v7 targets `gisebo-05`, an entirely new device.** It gets its own decoder from day one, pinned at `FIRMWARE_VERSION = 7`. Nothing ever reinterprets old bytes, so the byte-identical-layout ambiguity never arises in practice.

This also removes the plan's largest risk: the first board to run new firmware is **a bench board, not a production board on a post at a lake**.

**gisebo-05 replaces gisebo-01 at cutover** — the two are never in production together. So the freeze on gisebo-01 is **temporary**, and schema changes become legitimate at the swap. During development gisebo-05 can join and transmit freely: the webhook recipient discards data that does not fit, so v7 uplinks are dropped rather than disrupting the live pipeline.

That same discard behaviour is a **trap at cutover**: if telegraf/influx/grafana are not migrated to the v7 schema in the same operation, the site goes dark in the dashboards **silently**, because the recipient drops rather than errors. See the cutover gap below.

The {1, 7, 13} signature remains a useful fleet marker: any unit emitting it is pre-fix.

### Counter semantics the decoder must document

Incrementing only on `txComplete` gives three distinguishable cases, and all three are useful — but only if written down, or the backend will invent its own meanings:

- **+1** — normal.
- **Repeat (same value twice)** — a TX timed out and the batch was retried. `ramCount` is not cleared on timeout, so the next wake re-sends with the counter unchanged. This is a *retransmission marker*, not a duplicate to discard.
- **Gap (>1)** — an uplink was genuinely lost between device and network server.

### The reboot signal was in the metadata all along

`LMIC_reset()` in `setup()` clears `seqnoUp`, so **every** reboot restarts `f_cnt` at 0 — visible in TTN metadata, costing zero payload bytes. The 2026-07-16 capture shows continuous `f_cnt` (709→808, 376→391) precisely because no reboots occurred, not because they were undetectable.

Combined with `.noinit` (item 5) preserving the uplink counter across a soft reset while `f_cnt` resets regardless, the pair distinguishes reboot *kinds* with no payload at all:

| f_cnt | uplink counter | means |
|---|---|---|
| resets | resets | cold boot (`.noinit` invalid) |
| resets | continues | soft reset (join-failure path) |
| continues | gap | dropped uplink |

This makes the status byte's 3-bit boot counter and its cold-boot/soft-reset flags largely redundant — see the open question recorded in the uncertainty memory.

---

## 3. Regenerate test payload vectors

**Status:** Not started — **unblocked**: the live decoders are exported to `decoders/` and the stale repo copy is deleted, so the harness now has a real target. Still sequenced behind items 2, 8, 9 for the vectors themselves.
**Complexity:** Low
**Estimated time:** 5–7 h
**Sprint:** 01 (harness + primary vectors), 04 (solar vectors)

### Problem

`doc/test-payloads.md` documents Protocol V5 and is stale four ways:

1. **Wrong length.** Every vector is 8 bytes. The protocol is 9. `ttn-decoder-v6.js:15` rejects anything under 9 bytes, so **all four vectors fail the length check before a field is parsed**. They validate nothing.
2. **No interval coverage.** Nothing exercises `interval_index`, `interval_minutes`, `interval_label`, or the index 0 / >10 clamp branches at `:22-26`.
3. **Wrong battery framing.** Vector 2 is "The LiPo Peak" at 4.2 V. The primary build runs a 6 V pack; the solar build runs 1S2P li-ion. Neither is a LiPo.
4. **Semantics changing.** Item 2 redefines the counter; item 8 adds a 15-byte variant on FPorts 11/21.

The file also lives in `doc/` while every other document is in `docs/`.

### Solution

Rewrite as `docs/test-payloads.md`, covering both variants — see sprint tasks for the vector list. For each: FPort, hex payload, exact expected decoder JSON, derived from the decoder rather than hand-arithmetic.

The single most important vector: **solar, full sun, full pack — panel current ≈0 mA with a high bus voltage.** That is the charge-terminated case that looks identical to darkness if you only read current.

### Verification

Run every vector through `decodeUplink()` in the Node harness and diff against documented JSON. Not by eye — the file's current state is exactly what happens when vectors are hand-maintained and never executed.

### Notes

`ttn-decoder-v6.js:42` has a latent gap this will surface: bytes 201–249 and 253–255 match no branch, so those temperatures are silently **dropped** from the array rather than pushed as `null`, shortening it and misaligning every later reading against its timestamp. Not reachable from current firmware. Decide deliberately: document as out-of-contract, or push `null`. Do not paper over it by choosing vectors that avoid the range.

---

## 4. Split firmware into core + `PowerPolicy` architecture

**Status:** Not started — blocked on items 1, 2 landing first
**Complexity:** Medium
**Estimated time:** 14–18 h
**Sprint:** 02

### Problem

The sketch is a single 460-line `.ino` with one hardcoded power strategy. A second deploy target (solar, li-ion) needs different voltage bands, an extra sensor, and a longer payload — while the existing non-rechargeable build stays supported.

Running today's firmware unchanged on 1S2P li-ion pins the interval at index 10 (7 days) permanently: thresholds 5.0/4.3/3.5 V are all above a full li-ion's 4.2 V, so every reading scores `voltage_offset = 3`.

### Solution

Three parts, one binary (see `docs/solar-variant-design.md` § Architecture):

- **Core**: sensor read, batch buffer, payload assembly, LMIC, TX, sleep. `loop()`'s two-phase flow unchanged.
- **`PrimaryCellPolicy`**: today's algorithm, 6 V bands, 9-byte payload, FPorts 10/20.
- **`SolarPolicy`** (item 8): li-ion bands, INA219, 15-byte payload, FPorts 11/21.

Behind a virtual `PowerPolicy`. The season state machine is **shared**, not duplicated — both policies use the same seasonal baseline and differ only in bands and what they add.

The interface is not just a signature list; three call-site contracts are load-bearing and were previously implicit:

```c
class PowerPolicy {
public:
  virtual void    begin() = 0;
  // Called ONCE per wake, immediately after sensors.requestTemperatures(),
  // INSIDE the Dallas conversion window. Must return well under the window
  // (SolarPolicy: ~68 ms of INA219 averaging). See item 1 — the core measures
  // the window from requestTemperatures(), not from this returning.
  virtual void    onWake() = 0;
  // vbat is read by the CORE (A7 divider is board-level, not policy-level) and
  // sampled BEFORE LMIC_setTxData2(), so it is essentially open-circuit.
  virtual uint8_t decideInterval(float tempC, float vbat) = 0;
  // Appends after byte 8; returns bytes written (0 for primary, 6 for solar).
  // Bytes 0-8 are the core's and must stay byte-identical across variants.
  virtual uint8_t appendPayload(uint8_t *buf) = 0;
  virtual uint8_t fport(uint8_t runMode) = 0;
};
```

- **`onWake()` timing** — items 1 and 8 both depend on it and neither could state it alone. The core owns the window; the policy borrows part of it.
- **`appendPayload` returns the length** rather than a separate `payloadLen()` — one call, no chance of the two disagreeing.
- **The core reads VBAT**, not the policy. The A7 divider is a board property; both policies interpret the same reading through different bands.

Season sits **outside** the interface: both policies consume it, neither owns it. If either policy needs to reach into the season machine, the boundary is wrong.

**Variant selection is a runtime I2C probe** for the INA219 at 0x40 in `setup()`. One image for every board, consistent with the "compile once, flash everywhere" goal in `docs/generate-keys-from-feather-serial.md`. No ID register on the INA219, so: address ACK plus a config-register sanity read.

**Retry the probe — 3 attempts, 50 ms apart, before concluding absent.** A single boot-time probe means one transient I2C glitch selects the primary policy for the entire session, pinning a solar unit at a 7-day interval until something resets it. The probe runs once per boot and a boot is rare; there is no reason to trust a single attempt with that outcome. Three attempts cost 100 ms of a boot that already spends `delay(5000)` waiting for the radio to stabilise.

### Verification

Compile-only plus host-side tests of the extracted logic. `PrimaryCellPolicy` is behaviourally identical to today **except** for the voltage-band hysteresis (item 8), which is a deliberate, separate fix: the 5.0/4.3/3.5 bands are bare thresholds and `gisebo-04` at 5.233 V is drifting toward the 5.00 V edge. Land the refactor first with identical behaviour, then the hysteresis as its own commit, so the diff for each is readable.

### Notes — the probe's failure mode

A dead INA219, loose wire, or hung bus makes a **solar board boot the primary policy** and pin itself at a 7-day interval forever. A component fault silently decommissions the unit.

The FPort makes it observable: a board on FPort 10 reporting battery below ~4.5 V is definitionally misdetected, since no healthy 6 V pack sits that low. **The backend must alarm on that combination** (item 10). It costs nothing in firmware and it is the only thing between a loose connector and a unit that goes quiet for a year.

---

## 5. Persistent state across reset (`.noinit`)

**Status:** Not started — blocked on item 4
**Complexity:** Low–Medium
**Estimated time:** 5–7 h
**Sprint:** 03

### Problem

The PROD join-failure path calls `NVIC_SystemReset()` (`:404`), which re-runs C startup and zeroes `.bss` — wiping season state, counters, and (once item 8 lands) all solar history. Every join-failure reset restarts at Summer/index 2 and re-walks the season machine one step per uplink, with solar history empty — in winter, precisely when joins fail most and the wrong policy costs most.

The SAMD21 RTC also does not survive a system reset: no backup domain.

### Solution

A `.noinit` struct guarded by a magic word and a **layout version**. A soft reset does not physically clear SRAM — only the C runtime zeroes `.bss` — so `__attribute__((section(".noinit")))` survives `NVIC_SystemReset()` at zero flash cost, fully consistent with the no-FlashStorage rule. Magic or version mismatch → treat as cold boot, reinitialize, set the cold-boot flag.

Contents: season state, **latched voltage-band state** (item 8's hysteresis makes `voltage_offset` stateful, not a pure function of `vbat`), `currentIntervalIndex`, `lastTempC`, uplink counter, boot counter, RTC epoch, sun-presence EWMA, harvest accumulator.

**Deliberately NOT preserved: `dataBuffer` and `ramCount`.** Up to six buffered samples are lost on every join-failure reset, and that is correct rather than an oversight. The decoder extrapolates per-sample timestamps backwards from the uplink time assuming **uniform spacing at byte 0's interval**. Samples that straddle a reset do not have uniform spacing — the join-failure path burns 3 minutes of join attempts, 15 minutes of sleep, then a reboot and a fresh join, so a preserved buffer would carry a multi-interval hole that byte 0 cannot express. Preserving them would hand the backend confidently mis-timestamped data; dropping them costs at most six samples and the rejoin's fast-flush restarts the series cleanly.

If a future protocol carries per-sample timestamps rather than extrapolating, revisit — the objection is to the *extrapolation contract*, not to the data.

Read the RTC epoch after the 15-minute sleep, stash it, reset, restore on boot.

### Verification

Host tests for magic/version validity and cold-boot detection.

### Notes

The layout version is not optional: a firmware upgrade that changes the struct must not read the old layout as valid, or the device resumes from garbage — strictly worse than a cold boot. Bump on any field change. Add a `static_assert` on `sizeof(PersistState)` so a field change that nobody bumped fails the build rather than the field.

**Magic + version is not sufficient — add a CRC.** Those two catch layout changes and clean cold boots. They do **not** catch corruption, and this design leans on a physical claim: that SRAM retains contents across a soft reset. A brief power interruption decays RAM *partially* — long enough to corrupt the payload, short enough to leave a 32-bit magic word intact. The result is a confident restore from garbage, which is exactly the failure the version guard exists to prevent, arriving by a different door.

A CRC over the struct body costs a few bytes and one pass at boot. S06-06 tests this case on hardware ("confirm a brief power interruption does not produce a false valid magic word"); without a CRC there is no plan for what to do when that test fails.

Sketch:

```c
struct PersistState {
  uint32_t magic;       // fixed constant
  uint16_t version;     // bump on ANY field change
  uint16_t crc;         // over everything below
  // ---- body ----
  uint8_t  seasonState, voltageState, currentIntervalIndex, uplinkCounter, bootCounter;
  float    lastTempC, sunEwma;
  uint32_t rtcEpoch;
  uint16_t harvestMilliAmpHours;
} __attribute__((section(".noinit")));
// valid  <=>  magic ok AND version ok AND crc(body) ok. Any failure => cold boot.
```

---

## 6. RTC, wall clock, and `DeviceTimeReq`

**Status:** Not started — blocked on item 5
**Complexity:** Medium
**Estimated time:** 8–11 h
**Sprint:** 03

### Problem

The solar policy needs elapsed time (to decay an EWMA correctly) and benefits from absolute time (to know when the sun *should* be up). An earlier draft hand-rolled an elapsed-time accumulator by summing `sleepIntervalSeconds` — strictly worse than reading the RTC: it misses awake time and cannot survive a reset.

### Solution

The SAMD21 RTC counts through standby, and the Feather M0 carries an external 32.768 kHz crystal. The Arduino SAMD variant for this board does not define `CRYSTALLESS`, so `RTCZero` sources GCLK2 from that crystal: ~20–50 ppm.

**This is already in use.** `ArduinoLowPower` on SAMD wraps `RTCZero` — it holds its own instance, sets an alarm epoch, enters standby. `LowPower.deepSleep()` is already crystal-backed and already accurate. Adopting `RTCZero` explicitly does not unlock *sleeping*; it unlocks **reading** the time.

- **Ownership:** `ArduinoLowPower` keeps the sleep and idle paths; a separate `RTCZero` instance is added for `getEpoch()`.

  **The double-`begin()` is avoidable, and reading the source says it must be avoided.** `RTCZero::begin(false)` calls `RTCreset()`, which software-resets the peripheral and clears the counter. It only preserves the old clock value when `PM->RCAUSE` indicates a SYST/WDT/EXT reset — so a `begin()` called during normal operation, after a power-on reset, would **wipe the time**. `_configured` is an *instance* member, so a second `RTCZero` object believes the hardware is unconfigured and will happily reconfigure it.

  Fix: **never call `begin()` on our instance.** `getEpoch()` and `setEpoch()` only touch `RTC->MODE2.CLOCK.reg` — they do not test `_configured`. So let `ArduinoLowPower` own configuration and use a read-only instance on top:

  ```c
  RTCZero rtc;   // reads only — begin() is NEVER called on this instance

  void setup() {
    // Force ArduinoLowPower to configure the RTC once, via its own API.
    // This is exactly what setAlarmIn() does lazily; doing it here makes the
    // ordering explicit and means getEpoch() is valid before the first sleep.
    LowPower.attachInterruptWakeup(RTC_ALARM_WAKEUP, NULL, (irq_mode)0);
    ...
  }
  ```

  One `begin()`, from the library that owns the peripheral. Without the explicit `attachInterruptWakeup`, `ArduinoLowPower` configures the RTC lazily on the first sleep, so `getEpoch()` returns garbage until then — which matters because the first uplink after join happens *before* the first sleep.

  This still needs S06-03 to confirm on hardware, but it turns "unverifiable seam" into "specific claim to check".
- **Wall clock** seeded once via `DeviceTimeReq` on the first uplink after join. With this crystal one acquisition holds for months (~4 s/day), so it is a one-shot, not an ongoing dependency. Only arrives in an RX window after an uplink, and can simply not land.

  **Verified against MCCI LMIC v6.0.1 source** ([mcci-catena/arduino-lmic](https://github.com/mcci-catena/arduino-lmic), `src/lmic/`), correcting two earlier assumptions:

  - **`LMIC_ENABLE_DeviceTimeReq` defaults to 1** (`src/lmic/config.h:175-177`) — it does **not** need adding to `lmic_project_config.h`. An earlier draft said it did; that was wrong. Nothing to configure, and S02-17 loses that half of its scope.
  - **The callback does not carry the time.** The signature is `void cb(void *pUserData, int flagSuccess)` (`lmic.h:467`) — success/fail only. The time must be fetched inside the callback via `LMIC_getNetworkTimeReference(&ref)`, which fills `{ ostime_t tLocal; lmic_gpstime_t tNetwork; }` (`lmic.h:476-481`). An earlier draft assumed the epoch arrived as a callback argument.

  ```c
  static void onNetworkTime(void *pUserData, int flagSuccess) {
    lmic_time_reference_t ref;
    if (!flagSuccess || !LMIC_getNetworkTimeReference(&ref)) return;  // stays degraded
    // ref.tNetwork  = GPS seconds at ref.tLocal (an ostime_t, already back-adjusted
    //                 by LMIC to the nearest second boundary)
    // ref.tLocal    = os ticks; compensate for elapsed time since the uplink
    uint32_t utc = ref.tNetwork + 315964800UL - GPS_UTC_LEAP_SECONDS   // 18 as of 2026-07
                 + osticks2ms(os_getTime() - ref.tLocal) / 1000;
    rtc.setEpoch(utc);
    persist.clockValid = true;
  }
  ```

  `tLocal` is not decoration: it marks when the *uplink* left, and the callback runs after the RX window, so the offset must be added or the clock is seconds slow. There is no LMIC helper for GPS→UTC — the 315964800 offset and the leap-second constant are ours to own and date.
- **No clock is a supported state.** Until it lands (or forever, in poor coverage) the solar policy runs on the raw EWMA and the clock-valid flag reports the degraded state. Season is temperature-based regardless, so nothing else is affected.

### Verification

Host tests for GPS→UTC conversion (including the `tLocal` compensation and the leap-second constant) and the degraded path. The double-`begin()` interaction still needs hardware — S06-03, now testing a specific claim rather than a vague seam.

Source reference: [mcci-catena/arduino-lmic](https://github.com/mcci-catena/arduino-lmic) v6.0.1. A local clone may exist outside the repo; do not rely on a machine-specific path.

---

## 7. Day length and solar expectation

**Status:** Not started — blocked on item 6
**Complexity:** Low–Medium
**Estimated time:** 4–6 h
**Sprint:** 03

### Problem

The raw sun-presence EWMA conflates two things: how long the sun is up (season) and how often it actually shines (weather, or a fouled panel).

### Solution

With a valid clock and a known latitude, expected daylight fraction is computable, and the ratio separates them:

```
clarity = ewma / expected_daylight_fraction
```

~1.0 means clear skies. Persistently low means overcast — **or snow, leaves, or shade on the panel**, which a vertical mount in July would otherwise hide behind a plausible-looking low EWMA. A fault class the design currently cannot observe.

Costs **zero payload bytes**: the backend has the TTN uplink timestamp and the latitude constant, so it computes the expectation itself. Byte 11 stays the raw EWMA. Firmware needs day length only if the policy keys on clarity directly; otherwise this is entirely a decoder/backend item.

**Latitude lives in the decoder, per device — not in firmware.** This resolves a contradiction the plan carried until now.

Item 8's solar policy keys on the **raw EWMA**, not on clarity. So no firmware code path needs day length: the backend has the TTN uplink timestamp and computes the expectation itself. Day length is **100% a decoder concern**.

That dissolves the one-binary problem entirely. The earlier worry — "a hardcoded latitude breaks compile-once-flash-everywhere and fails silently if a unit moves" — only applied if the constant were in firmware. It is not. Decoders are set **per device** in TTN, so latitude sits alongside `FIRMWARE_VERSION` (item 9) as a per-device decoder constant:

```js
const FIRMWARE_VERSION = 7;
const SITE_LATITUDE_DEG = 57.81;   // per device; a degree or two is minutes of day length
```

Firmware stays latitude-free and genuinely one-binary. A relocated unit needs its decoder constant updated, not a reflash.

**Consequence for the sprint plan:** S03-15/16/17 were written assuming firmware might need day length. With this settled they are decoder work and belong beside item 9 in sprint 04, not in sprint 03's firmware block. S03-15 already says "decide first: firmware or backend? Prefer the backend" — this is that decision, made.

### Notes — season stays on water temperature

Day length was considered as a season signal and **rejected**. The season machine exists to sample less when the tank changes slowly (master-plan); water temperature reads that directly, where day length is an astronomical proxy for weather which is a proxy for the tank — two steps removed, and wrong during an unseasonable warm spell. Day length's advantages are drift-freedom and predictability: the flapping it would prevent is already prevented by the 1 °C hysteresis, and the efficiency it would optimize is worth under 4% (item 11). Each signal is used for what it is good at. The season machine therefore stays shared, and the primary variant is unaffected.

---

## 8. Solar policy and INA219

**Status:** Not started — blocked on items 4, 5, 6
**Complexity:** Medium–High
**Estimated time:** 16–20 h
**Sprint:** 04

### Problem

New deploy target: post-mounted, true-south, vertically-mounted 5 V 0.15 W panel charging a 1S2P 18650 pack through an INA219, supercap across the pack to cover the radio's 120 mA TX spike in the cold.

### Solution

Full detail in `docs/solar-variant-design.md`. Summary:

- **Bands: 3.85 / 3.65 / 3.45 V** (healthy/low/critical), **with 50 mV hysteresis** — see below. Healthy at 3.85 is ~60%+ SOC, where a solar-fed pack should live, so the bonus is usually available. Critical at 3.45 keeps margin above the ~3.4 V brownout. VBAT is sampled at wake, before `LMIC_setTxData2()`, so it reads essentially open-circuit — cold sag under TX load never enters the measurement, and the supercap covers it anyway.
- **Interval:** `season_base` then, **only if `voltage_offset == 0` AND the solar bonus is latched on** (EWMA > 0.55 to engage, < 0.45 to release), subtract a **fixed 2 steps**. Clamp [2, 10]. Decided 2026-07-17.

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

  Expressed as a *measurement*, not a season state — so an unusually bright spring qualifies and an overcast summer week does not. That is the point of using the EWMA rather than `season == Summer`.

  Two gates, not one. `voltage_offset == 0` means sun never shortens the interval on a struggling pack, so the signals cannot fight — and it is also what makes the no-MPPT decision safe, since motorboating only corrupts the bus voltage while charging, i.e. exactly when the pack is not full and the bonus is off anyway (item 11). The loop self-corrects: if shortening outruns harvest, the pack drains, `voltage_offset` leaves 0, the bonus disappears, and the interval returns to baseline.

  **The threshold is provisional.** No field EWMA distribution exists — nothing has ever measured one. Byte 11 uplinks the raw EWMA precisely so it can be tuned from real data rather than from this estimate.
- **Floor index 2 (5 min), batch stays 6.** ~48 uplinks/day, well inside TTN's 30 s/day fair use.
- **Signal:** panel bus voltage is a clean day/night discriminator — at night the panel is dark, the Schottky blocks, the bus sits near 0 V; with sun the bus reads the charger's operating point (~4.5–5 V) or, once charge terminates, panel Voc. So `sun_present = (bus_mV > 3000)`.

  **This threshold deliberately gets no hysteresis**, unlike the voltage bands and the bonus gate. It feeds an EWMA, so dithering at dawn and dusk contributes *fractionally and correctly* — a boundary sample that lands either side is genuine information about a marginal moment, not a spurious state flip. Adding hysteresis here would bias the daylight-fraction estimate the EWMA exists to measure. The mitigation for its real noise source (motorboating) is the INA219's 128-sample averaging, already decided (item 11).
- **Deliberately not current.** The INA219 measures *harvested* current, not available sunlight: when the pack is full the MCP73831 terminates and current collapses to ~0. With the claimed surplus, that is most of the summer. Current is still uplinked and accumulated for energy analysis — it is just not what the policy keys on.
- **Time-based window.** A window of N wakes is a window of N × interval seconds, and the interval is what the window controls: a sunny afternoon shortens the interval, shrinking the window to hours that are still all daylight, so it never sees the night that should pull the average down. It hunts on a multi-day period. Take `dt` from the RTC and decay: `alpha = 1 - exp(-dt/TAU)`, TAU = 86400 s. `exp()` once per wake is free. Do **not** use the linear approximation `alpha ≈ dt/TAU` — it errs ~13% at a 6 h interval, exactly the winter case.
- **Read every wake, inside the Dallas window**, via `PowerPolicy::onWake()` — the core measures the window from `requestTemperatures()` and the policy borrows ~68 ms of it for INA219 averaging (item 1, item 4). Sampling every wake, not once per uplink, is what makes a clockless day/night estimate work at all: at a 30 min interval with batch 6, one sample per message would be one sample every 3 hours at an arbitrary time of day.
- **Calibration:** `setCalibration_16V_400mA()` for a 0.1 mA LSB. The breakout's 32 V/2 A default gives 0.8 mA LSB — ~4% resolution against a 30 mA panel.


### Voltage-band hysteresis — required, both variants

**The voltage bands are bare thresholds and will dither.** The A7 divider gives a 6.45 mV LSB (10-bit, 3.3 V ref, ×2), and a single SAMD21 ADC sample carries ~±13–19 mV of noise. A pack sitting within that of a band edge flips `voltage_offset` **every wake** — not daily, every wake.

The consequences are not cosmetic:

| variant | edge | gates | flip cost |
|---|---|---|---|
| solar | 3.85 V | `voltage_offset` 0↔1 — **the solar bonus gate** | interval 2↔4 (5 min ↔ 30 min) |
| solar | 3.65 V | 1↔2 | interval 4↔5 |
| primary | 5.00 V | 0↔1 | interval 4↔5 (30 min ↔ 60 min) |

**Correction: this is not live in production.** An earlier draft claimed `gisebo-04` was drifting toward the 5.00 V edge. It runs V5 firmware with a *fixed* 5-minute interval — no dynamic interval, no voltage ladder, nothing to dither — and is never being reflashed. `gisebo-01` sits at 5.768 V, 0.77 V clear of its edge.

It matters because of **gisebo-05**, which is solar: its 3.85 V edge *gates the solar bonus*, and a li-ion pack lives on the 3.6–3.9 V plateau, so it will spend real time within ±19 mV of that edge — each flip swinging the interval 5 min ↔ 30 min, wake to wake. The season machine got 1 °C of hysteresis for exactly this class of problem; the voltage ladder never did.

**Rule: degrade at the nominal edge, improve at nominal + 50 mV.** Asymmetric on purpose — react promptly to a failing pack, recover reluctantly. The protective response is never delayed, and 50 mV comfortably exceeds the ±19 mV noise band.

```c
#define VOLTAGE_HYST_V 0.05f   // > 2x single-sample ADC noise (~19 mV)

// Latched, like current_season_state. Lives in .noinit (item 5).
static uint8_t voltageOffset(float v, uint8_t prev, const float edge[3]) {
  for (uint8_t i = 0; i < 3; i++) {
    // Improving into band i needs +hyst; holding band i only needs the nominal edge.
    float e = edge[i] + ((prev > i) ? VOLTAGE_HYST_V : 0.0f);
    if (v >= e) return i;
  }
  return 3;
}
```

`voltage_offset` therefore becomes **latched state, not a pure function of `vbat`** — it must persist across wakes and belongs in `.noinit` beside `current_season_state` (item 5). That is the same shape the season machine already has, so it is consistent rather than novel.

**Also average the ADC.** `getBatteryVoltage()` currently takes two dummy reads then **one** real sample (`adafruit-feather-m0-lora-ttn-2.ino:26-30`). Averaging 16 samples costs microseconds and cuts noise by 4× — attacking the dither at its source rather than only masking it downstream. Do both: averaging narrows the noise, hysteresis absorbs what is left.

### Verification

Host tests only. No device. On-device bring-up is item 12. Test the hysteresis with a **noisy** input, not clean steps: feed a pack voltage sitting on an edge with ±19 mV of simulated noise and assert `voltage_offset` never changes. A single-point test cannot see this defect.

---

## 9. Decoder v7

**Status:** Not started — blocked on items 2, 8
**Complexity:** Medium
**Estimated time:** 8–11 h
**Sprint:** 04

### Problem

The live decoder handles 8- and 9-byte shapes, hardcodes `version: 5`, reports a `rebootDetected` field that has never fired, and silently drops out-of-contract temperature bytes.

**v6 is current; this work is v7** — covering *both* the 9-byte primary with uplink-counter semantics and the 15-byte solar payload. See `docs/solar-variant-design.md` § Protocol versioning.

### Solution

**Re-examine "one combined decoder" — 2026-07-17.** The export changed the facts it rested on.

There is **no application-level formatter**. Both live devices carry their **own**, and they **differ** — gisebo-01's is 9-byte aware, gisebo-04's is 8-byte only with a hardcoded interval. Neither branches on length; that was an inference from `decoded_payload`, and it was wrong. Per-device decoders are not a new idea to introduce; they are how this already runs.

More decisively: **gisebo-01 is production and frozen**, so a combined decoder could never be deployed there without reproducing its exact output schema byte-for-byte — including the `version: 5` bug and the phantom entries. The drift argument for combining assumed one artifact serving every device. That is not achievable.

**So: leave gisebo-01's and gisebo-04's decoders alone. Write a fresh one for gisebo-05, pinned at `FIRMWARE_VERSION = 7`.** There is nothing to combine — gisebo-01's decoder is frozen and then **retired**, because gisebo-05 *replaces* it at cutover. Two decoders during development, one after. The old two are frozen records in `decoders/`, not living code, and the drift argument does not apply to files nobody maintains.

The `FIRMWARE_VERSION` constant survives this and is still worth having: it is what lets the S01-04 harness exercise v5/v6/v7 semantics against one file, and it documents at a glance which firmware a decoder is pinned to.

- Per-device constant, the only thing not derivable from the payload:
  ```js
  // Set per device to match the firmware flashed on THIS unit.
  // 5 = 8-byte legacy | 6 = 9-byte wake-counter | 7 = 9-byte/15-byte uplink-counter
  const FIRMWARE_VERSION = 7;
  ```
  Decoders are set per device in TTN, and with two units where a reflash is a site visit, provisioning knows what is flashed. Used **only** to resolve the wake-vs-uplink counter ambiguity; everything else derives from length and FPort.
- **Derive the `version` field** rather than hardcoding it. The live decoder reports `version: 5` for `gisebo-01`'s 9-byte v6 payloads — a static string, not a derived value. Cosmetic in effect but actively misleading: it caused a false diagnosis during planning that TTN was misdecoding v6 as v5.
- 15-byte payload on FPorts 11/21; 9-byte on 10/20. Bytes 0–8 byte-identical across both.
- Parse panel V (30 mV/LSB), panel I (0.5 mA/LSB), sun EWMA (0–255 = 0.0–1.0), harvest accumulator (1 mAh/LSB, 16-bit), status byte (bits 7–5 boot counter, bits 4–0 flags: cold boot, soft reset, clock valid, solar bonus active, last TX timed out).
- `uplink_counter` replaces `sequence`/`rebootDetected` (item 2).
- Compute `clarity = ewma / expected_daylight_fraction` from the uplink timestamp and the latitude constant (item 7).
- Decide the 201–249 / 253–255 handling (item 3).

---

## 10. Backend alarms and monitoring

**Status:** Not started
**Complexity:** Low
**Estimated time:** 5–7 h
**Sprint:** 01 (misdetect alarm), 05 (rest)

**2026-07-29 additions from the code review and item 24b.** Two backend
responsibilities accumulated here because the payload and a stateless TTN
formatter cannot carry them:

1. **Clarity convergence gating for DATA frames.** Schema-2 verbose frames now
   gate `clarity` on the device-reported uptime, but data frames (FPort 11/21)
   carry no age field, so the formatter emits their clarity ungated. The
   backend has history, so it can apply the same rule: suppress or flag
   clarity for ~24 h (one `SUN_EWMA_TAU_S`) after an `f_cnt` reset /
   `cold_boot`. `expected_daylight_fraction` is emitted alongside for exactly
   this purpose.
2. **The robust solar-input metric is the battery-voltage TREND, computed
   here.** Decided 2026-07-29 (`docs/solar-input-measurement-research.md`):
   `harvest_mah` carries a structural ±40%-class error in PROD, while the pack
   itself integrates net charge physically and `battery_v` rides every frame.
   Compute a multi-day, temperature-corrected slope (the water temperature is
   in the same frame; the coefficient is measured at +12.8 mV/degC on
   alkaline, smaller on li-ion) exactly as done by hand for gisebo-01/04.
   Slope >= 0 means harvest covers consumption, which is the question that
   matters.

3. **Cutover schema inventory.** The decoder now also emits `uptime_s`,
   `uptime_h`, `cycle_count`, `ram_count`, `panel_ma_min/mean/max`,
   `panel_v_min/max`, `clarity_converging`, and the `ina219_ovf` fault name.
   The shared webhook discards unknown fields during development, but the
   telegraf/influx/grafana migration at cutover must decide which of these to
   persist — silently dropping them is the same trap the cutover section of
   the fleet notes already warns about.

### Problem

Uplinks are the only instrument — there is no bench testing and no test hardware. Several failure modes are silent unless the backend watches for them.

### Solution

- **Probe misdetect** (item 4): alarm on FPort 10 with battery < 4.5 V. The single highest-value alarm; without it a loose INA219 connector silently parks a unit at a 7-day interval.
- **Clock never acquired** (item 6): alarm on clock-valid false after N uplinks.
- **`idle(750)` regression** (item 1): alarm on **252 in temperature slot 0 of any FPort 10/11 uplink**. Not "fast-flush uplinks" — the backend cannot identify those, and after item 2's fix the fast-flush fires once per join rather than every 16 wakes. Any 252 in slot 0 is worth alarming on regardless: real water above +30 °C should be rare-to-never at these sites, so a sentinel there means the sensor returned its 85 °C reset default.
- **Pack health trend** (item 11): resting voltage trending down over months while harvest stays normal. The supercap deliberately hides TX sag, so internal resistance cannot be inferred from a 50 ms transmit — this slow signal is the only one available, and it is what makes the 5–10 year replacement cycle actionable.
- **Panel fouling** (item 7): clarity ratio persistently low against a high expectation.

---

## 11. Hardware BOM decisions

**2026-07-29 addition — coulomb counter for any future board revision.** If
gross harvest measurement ever becomes a requirement, the correct part is an
LTC2942-class I2C gas gauge (<100 µA, integrates through the sense resistor
while the MCU sleeps; LTC2941 is EOL, prefer LTC2942/2944) — and explicitly
NOT an INA228/229: their charge/energy accumulators only run in continuous
mode (~640 µA), which consumes the harvest being measured. Rationale and
sources in `docs/solar-input-measurement-research.md`.

**Status:** Partially resolved — **No MPPT — decided 2026-07-17**; first order placed
**Complexity:** Research
**Estimated time:** 3–5 h remaining
**Sprint:** 01 (spikes), 07 (second order)

### No MPPT — decided 2026-07-17

The panel feeds the Feather's USB pin through a Schottky and the INA219, charging via the onboard MCP73831. **The topology stands as originally designed** — no rework.

It will motorboat: a ~30 mA panel against a 100 mA charger collapses the panel, trips UVLO, recovers, restarts, costing perhaps 30–50% of harvest. Accepted because:

- **No energy case.** ~10× claimed summer surplus, 500 days of dark-spell reserve. Halved harvest still leaves ~5×. This was never an energy decision.
- **The healthy-battery gate already covers the signal risk.** Motorboating only happens while *actively charging*; a full pack terminates the charger, the panel goes unloaded, and the bus sits cleanly at Voc. The solar bonus is gated on `voltage_offset == 0` — a near-full pack — so **the corrupted EWMA and the decision that consumes it never overlap.**
- **The INA219's 128-sample averaging (~68 ms) is a free mitigation**, fitting inside the 750 ms Dallas window. It averages across the oscillation instead of point-sampling it — a single read per wake would catch the cycle mid-collapse or mid-recovery at random, and the accumulator would integrate coin flips, biased unpredictably rather than merely noisy.
- **An external charger is not a drop-in**: `panel → INA219 → charger → BAT pin` bypasses the onboard charger, and plugging USB in DEV puts two chargers on one pack.

Accepted costs: the harvest accumulator carries an error bar (S07-06), the fouling alarm must gate on a full pack (S07-06), and **there is no hardware fallback**. S07-05 measures the motorboat period against the 68 ms window — that is the one measurement that could reopen this, and the remedies would be longer averaging, multiple reads per wake, or a wider error bar.

Rejected, recorded so nobody re-runs it: **bq24074** (no amazon.se listing; an Electrokit/Digikey/Mouser part), **CN3065** (right chip for a 5 V panel, but its input-regulation loop was never confirmed from the Consonance datasheet), **CN3791** (the amazon.se listings are **12 V variants** whose MPPT setpoint a 5 V panel can never reach — it would never charge at all).

### Ordered — 2026-07-17

Feather M0, DS18B20, INA219, panel, 18650 pack. ETA "later than sprint 03", not firm. This unblocks sprint 06 and most of sprint 07.

### Still open

- ~~**Supercap voltage rating.**~~ **Resolved 2026-07-17 (S01-13): a 1 F, 5.5 V module (≈0.5 F effective).** Size for the *cold RX window* (60 mC → 0.33 F), not the TX burst (6 mC → 0.03 F) — an 11× difference, and a 0.1 F part would cover the burst everyone talks about while failing the case the cap exists for. A 5.5 V module is two 2.7 V cells in series, so it halves the marked value. Leakage ~5–10 µA = 2–3% of the sleep budget; use the 60 °C curve, not 25 °C. **Open: inrush** — 0.5 F across a fresh pack is a short until charged.
- ~~**Over-discharge protection.**~~ **Resolved 2026-07-17 (S01-14): plain cells with ONE pack-level PCM, not protected cells.** A standard 1S PCM cutoff (2.4–2.8 V) lands correctly — below the 3.4 V brownout so firmware and MCU act first, above the ~2.5 V damage onset so the cell survives. **Not protected cells**: this is a 2P pack, and two independent PCMs in parallel means one trips, the other inherits the load and trips too, and they fight on recovery. Add the PCM's few µA to the sleep budget.
### Accepted, not open

- **Quiescent draw.** Accepted for the solar variant on the grounds that solar makes it affordable. **That reasoning does not extend to the primary variant.** From the brief's own figures a 12× interval change (30 min → 6 h) moves consumption 31%, which backs out to ~6.9 mAh/day (~290 µA) drawn regardless of wake count — consistent with a stock Feather M0's regulator, charger, and USB leak. That means ~a year on 4×AA and nearer seven months on 2× CR123A, and the interval ladder saves under 4%. If multi-year life on primaries is ever a requirement, the answer is quiescent-current surgery, not interval tuning.

### Notes — accepted risks

**Subzero charging** is accepted: cells are consumable on a 5–10 year cycle. The rationale holds better than it first appears — plating is strongly rate-dependent and the charge rate is panel-limited to ~30 mA into ~6000 mAh, about C/200. The MCP73831 will ask for 100 mA and never get it. C/200 into a cold cell is far gentler than the C/2-and-up regimes the "never charge below freezing" rule addresses. Consequence: replacement timing becomes a telemetry problem (item 10).

**The floor rests on an unmeasured number.** The brief's figures imply ~0.075 mAh per wake; a model of the wake (750 ms conversion plus an occasional 3 s TX) suggests closer to 0.002 mAh. At 5-minute sampling that is 28 mAh/day versus 7.6 mAh/day. The harvest accumulator will settle it from field data within a few months. Until then index 2 is defensible mainly because the healthy-battery gate self-corrects.

---

## 12. On-device verification

**Status:** Gated on delivery — **hardware ordered 2026-07-17**, ETA later than sprint 03, not firm
**Complexity:** Medium
**Estimated time:** 24–26 h across two sprints
**Sprint:** 06 (core), 07 (solar bring-up)

### Problem

Hardware arrives later than sprint 03, so verification cannot fold into the sprints that make the changes. **Every firmware item above ships verified by compilation and host-side tests alone**, and sprints 06–07 verify retroactively — each carrying a 4 h remediation buffer, because the realistic expectation is that verification finds something.

Ordered: Feather M0, DS18B20, INA219, panel, 18650 pack. Supercap and over-discharge protection follow in S07-08.

Things that cannot be verified any other way:

- The `idle(750)` fix actually waits, on a **PROD-strapped** unit — never DEV, since the bug does not exist there.
- `ArduinoLowPower` + `RTCZero` double-`begin()` does not collide over the RTC (item 6).
- INA219 wiring, address, and calibration are right (item 8).
- The I2C probe correctly selects the policy, and correctly *fails* when the INA219 is disconnected (item 4).
- `DeviceTimeReq` actually lands through the real gateway (item 6).
- Sleep current is what the budget assumes (item 11).

### Notes

**Serial and solar are mutually exclusive — DEV mode and solar are not.** USB puts 5 V on the same pin the panel feeds, so the Schottky blocks the panel and the INA219 reads ~0 mA on a 5 V bus. But **DEV mode is set by the strap (pin 11), not by USB presence**: strap DEV and leave USB unplugged and you get FPort 21, the busy-wait sleep path, no serial (the `while (!Serial)` wait simply times out) — and the panel feeding the USB pin normally, so the INA219 sees real solar. That is exactly what S07-04 does. The thing you cannot have is serial logs *while* observing solar; telemetry goes over the air instead.

---

## 15. Bench-verify battery and solar (INA219) metering against ground truth

**Complexity:** Medium
**Estimated time:** 5–7 h
**Sprint:** 07 (solar bring-up); overlaps S07-01/04/05

### Problem

The power telemetry has never been checked against a reference. `getBatteryVoltage()` reads A7 through a 100k/100k divider; the solar path reads panel **bus voltage** and current from the INA219 (16 V/400 mA calibration, 0.1 mA/LSB) and feeds the sun EWMA and the harvest accumulator. All of this is host-tested *logic* and compile-verified *glue* — but the actual ADC/I2C readings, the divider ratio, and the INA219 calibration are unverified on silicon. A wrong divider or calibration silently skews the battery bands (and thus the interval ladder) and the harvest figure, with no symptom in the data.

**2026-07-30 — the error bar is now measured over a FULL day, not four hours.**
Post-flash, comparing each cycle's harvest delta against that hour's true mean
from the minute-spaced profile: **cumulative −29.7%** (365 mAh reported against
519 mAh true), worst daylight hours **−69%** (14:34: 34 vs 110) and **−63%**
(13:33: 42 vs 115), with the sign flipping both ways (**+28%** at 12:33,
**+67%** at 06:36). Dusk and dawn are the worst relative offenders because the
integrand is changing fastest there. Confirms the reclassification below: no
constant can correct a signed, hour-varying aliasing error.

**2026-07-29 update — the harvest error bar is ANSWERED, and it is structural.**
The schema-2 panel profile measured the wake-sample-vs-hourly-mean error on
live data: up to 63% in one hour, ~36% cumulative over four hours, sign varying
hour to hour, so no calibration constant can tune it out. Point-sampling a
sleeping device aliases against cloud timescales, full analysis and the
weighted alternatives (coulomb counter, INA228, micro-wake sampling, battery
trend) in `docs/solar-input-measurement-research.md`. Consequences for this
item: `harvest_mah` is an indicative diagnostic, not a measurement; the DMM
sweep below still verifies the INSTANTANEOUS channels (bus V, current,
battery divider), which remain meaningful; the "harvest accumulator vs
timed current" step should verify the integration ARITHMETIC only, not chase
absolute accuracy PROD cannot deliver.

**2026-07-28 update — the item's fear was justified, and the first live night caught it.** Every post-boot INA219 reading was frozen (`powerSave(true)` was never undone; fixed in `007a46b`, `docs/dev-notes/20260728-1230_ina219-powersave-freeze.md`), so no telemetry before that fix says anything about metering accuracy. After re-flashing, first check the qualitative signature (panel V/I move frame-to-frame; `panel_v` collapses after sunset; `harvest_mah` flat at night); this item's PSU/DMM sweep then remains the quantitative pass.

### Procedure (turnkey)

Read values over the air: strap **DEV** and use the FPort 2 diagnostic frame (`battery_v`, `ina219_config`, `ina219_seen`) plus item 16's FPort 3 verbose frame (the full set) — no USB needed, and on solar it must be USB-free anyway (serial and solar are mutually exclusive; item 12 Notes). Record one row per setpoint in a `docs/dev-notes/` table.

1. **Battery divider (A7).** Bench PSU on the battery input; sweep **3.3, 3.85, 4.3, 5.0, 6.0, 6.6 V**. Record reported `battery_v` vs DMM. Confirm the 100k/100k ratio (×2) and that readings saturate ≈ **6.59 V** (so the 7.095 V payload clamp is genuinely unreachable). **Pass:** |reported − DMM| ≤ **2 %** below saturation.
2. **Panel bus voltage (INA219).** Apply **3.0–7.0 V** to the panel input; record `panel_v` vs DMM. **Pass:** ≤ **2 %**.
3. **Panel current (INA219).** Known resistive load, DMM in series; set ≈ **5, 20, 50, 100 mA**. Record `panel_ma` vs DMM; confirm the 16 V/400 mA calibration (0.1 mA/LSB) and that reverse current reads **0** (Schottky + `< 0 → 0` clamp). **Pass:** ≤ **5 %** or ≤ 0.5 mA, whichever is larger.
4. **Harvest accumulator.** Hold a known current *I* for a timed interval *t*; expected Δ`harvest_mah` ≈ *I·t*/3600. Repeat at two currents; record the error and **state the no-MPPT error bar** (feeds S07-05/06).
5. **Sun EWMA / bonus gate.** Raise/lower the bus voltage across `sunPresent()`'s threshold; confirm `sun_ewma` climbs/decays and `bonus_active` latches only with `voltage_offset == 0` **and** the sun gate.

### Verification / exit criteria

All four channels within tolerance; `ina219_config == 0x399F`; harvest error bar quantified. Any out-of-tolerance channel → adjust the divider constant / INA219 calibration in the sketch and re-run. Ties to the sprint-06/07 hardware checklist (INA219 wiring/calibration, per-wake charge).

---

## 16. DEV-strapped verbose over-the-air diagnostics ("all-clear") frame

**Status:** **Implemented 2026-07-27** (`0ba9fb0`, `docs/dev-notes/20260727-1804_verbose-dev-diagnostics-frame.md`); host + decoder tested, compiles. Pending on-hardware verification (flash gisebo-05 DEV, confirm ~hourly FPort-3 frames; a PROD unit emits none).
**Complexity:** Medium
**Estimated time:** 6–8 h
**Sprint:** 07 (paired with item 15)

### Problem

The diagnostics frame (`diagnostics.h`, FPort 1/2) is **fault-focused** — it fires on a new fault plus one boot frame, and its payload is a compact bitmap. During bench/DEV bring-up there is no USB-free way to watch the *full* live state and confirm everything looks OK (not just "no faults"): battery/panel/EWMA/harvest/season/interval/sensor values. USB is unavailable or avoided (and, on solar, mutually exclusive with observing the panel — item 12 Notes), and data frames are batched hours apart.

### Design (settled — turnkey)

**FPort 3, DEV-only.** No PROD counterpart; PROD must never emit it.

**Cadence: ~hourly, regardless of wake interval.** DEV never deep-sleeps (its "sleep" is a busy-wait running `os_runloop_once()`), so `millis()` advances normally — use a plain `millis()` gate, no clock or `.noinit` dependency. Send once on the first operational cycle after join, then whenever `millis() - lastVerboseMillis >= VERBOSE_INTERVAL_MS` (3 600 000). **Battery is deliberately not a concern in DEV**, so the hourly frame is unconditional — no fault/rate gating like the FPort 1/2 frame.

**Payload — FPort 3, schema 1, fixed 22 bytes, big-endian (matching the temp/battery convention):**

| byte | field | encoding |
|---|---|---|
| 0 | schema | `1` |
| 1 | info bits | b0 solar · b1 dev(=1) · b2 cold_boot · b3 clock_valid · b4 ina219_seen · b5 bonus_active · b6 sensor_bus_ambiguous |
| 2 | reset_cause | `PM->RCAUSE` |
| 3 | boot_counter | full 8-bit (not the 3-bit status-byte field) |
| 4 | interval_index | 0–10 |
| 5 | season \| band | b0-1 season (0 Summer, 1 Fall/Spring, 2 Winter) · b2-3 voltage_offset (0–3) |
| 6–7 | battery_mV | uint16 |
| 8–9 | panel_bus_mV | uint16 |
| 10–11 | panel_current | uint16, **0.1 mA/LSB** (finer than the data frame's 0.5) |
| 12 | sun_ewma | 0–255 = 0.0–1.0 |
| 13–14 | harvest_mAh | uint16 (wraps; backend unwraps) |
| 15–16 | ina219_config | uint16 (`0x399F` = healthy) |
| 17 | ds18b20_count | devices on the OneWire bus |
| 18–19 | surface_temp | int16 centi-°C, `0x7FFF` = invalid/NaN |
| 20–21 | fault_bits | uint16, same bitmap as `diagnostics.h` (so "all-clear" = `0x0000` is explicit) |

FPort map after this: data 10/20 (primary) · 11/21 (solar); faults 1 (PROD) / 2 (DEV); **verbose 3 (DEV)**.

### Implementation

- **`diagnostics.h`:** add `struct VerboseSnapshot { … }` (every field above) and `uint8_t diagEncodeVerbose(uint8_t *buf, const VerboseSnapshot *v)` returning 22. Add a pure gate `bool verboseShouldSend(bool isDev, bool sentOnce, uint32_t nowMs, uint32_t lastMs, uint32_t intervalMs)`. Both host-tested in `test/host/test_diagnostics.cpp`.
- **`.ino`:** `#define VERBOSE_FPORT_DEV 3`, `#define VERBOSE_INTERVAL_MS 3600000UL`; statics `lastVerboseMillis`, `verboseSentOnce`. In `loop()` operational, after `evaluateAndMaybeSendDiag()`: gather a `VerboseSnapshot` from live state (`solarPolicy`, `currentIntervalIndex`, `surfaceTempC`, the `g_*` diag inputs), and if `verboseShouldSend(...)` send it on `VERBOSE_FPORT_DEV` via a `sendDiagFrame`-style path (reuse the `OP_TXRXPEND` guard + bounded TXCOMPLETE wait; **out-of-band** — must not touch `currentIntervalIndex` or the uplink counter). Update `lastVerboseMillis`/`verboseSentOnce` only on a completed TX. The whole path is gated by a single `runMode == 1` check.
- **`decoders/gisebo-05-v7.js`:** add `decodeVerbose(bytes)` for FPort 3, dispatched before the data-FPort checks (like the diag branch); emit all fields plus `mode`, `run_mode`, `faults[]`, `healthy`. Add crafted vectors to `test/run.js`; add a real captured frame once flown.

### Verification

- Host: `verboseShouldSend` sends once at boot then hourly, and returns **false whenever `isDev` is false** (PROD never sends); `diagEncodeVerbose` places every field at its mapped byte.
- Decoder: round-trips a crafted 22-byte frame; wrong length errors loudly.
- On hardware: a **PROD-strapped** unit produces **zero** FPort-3 frames; a **DEV** unit produces one ~hourly whose decoded fields match the item-15 bench reference.

This extends the error/diagnostics frame that shipped 2026-07-26 (`docs/dev-notes/20260726-1905_diagnostic-error-uplink.md`, verified on gisebo-05's first flashed boot). It is the tool that makes item 15's power-metering verification observable over the air.

---

## 17. Verify `.noinit` survives the real reset path (`NVIC_SystemReset`)

**Status:** Observation logged 2026-07-27 (`docs/dev-notes/20260727-1901_noinit-did-not-survive-rst-pin.md`). Not started.
**Complexity:** Low–Medium
**Estimated time:** 3–5 h
**Sprint:** 06 (folds into S06-06)

### Problem

On gisebo-05, a physical **RST-button press** (`reset_cause: external`) produced
`cold_boot: true`, `boot_counter: 1` — the `.noinit` `PersistState` did **not**
survive (season/interval/harvest/clock reset to defaults). Likely the button
glitched the 3.3 V rail → partial SRAM decay → the CRC correctly cold-booted
(`persist.h` working as designed). **But the path that actually matters —
`NVIC_SystemReset()`, the PROD join-failure reset that the clock-preservation
(S03-06) depends on — is unverified.** An RST-pin press is a harder reset than
`NVIC_SystemReset()`, so it does not stand in for it.

### Solution / verification

1. Trigger `NVIC_SystemReset()` controllably — force a join failure (bad AppKey, or
   no gateway) to hit the PROD 3-min timeout → 15-min sleep → reset, or add a DEV
   test hook — and confirm `boot_counter` **increments** and season/interval/harvest
   carry over (i.e. `cold_boot: false`).
2. Characterise the RST-pin and watchdog resets for completeness.
3. Confirm a brief power interruption yields a cold boot (CRC catches false-valid
   magic) — the standing **S06-06** test.

Now cheap to observe over the air: the diagnostic (FPort 2) and verbose (FPort 3)
frames report `reset_cause`, `cold_boot`, and `boot_counter` directly.

### Impact if it does not survive

Recoverable but real: season re-enters at Summer (~2 uplinks to settle), interval
resets to 5-min initial, the **harvest accumulator resets to 0** (visible backend
discontinuity), clock re-requested, uplink counter resets. The harvest
discontinuity and the clock-preservation dependency are the ones to confirm.

---

## 18. Use the INA219 status flags: gate the read on CNVR, report OVF

**Status:** **IMPLEMENTED 2026-07-28** (`774021e`/`555406d`/`01363ab`) — `docs/dev-notes/20260728-2200_cnvr-gated-ina219-read.md`. Decoder half live (formatter re-uploaded, byte-identical); firmware pending flash. Resolved the open decision: CNVR timeout reuses `DIAG_FAULT_INA219_READ_FAIL`; OVF got the new bit `0x0080` (`ina219_ovf`). Also went beyond the spec: a failed read is no longer ingested at all. Flash-verify: healthy unit identical; SDA pulled or part held in power-down must raise `ina219_read_fail` within one cycle.
**Complexity:** Medium
**Estimated time:** 4–6 h
**Sprint:** 07

### Problem

The bus-voltage register (`02h`) carries the only two status bits the INA219
has, and `Adafruit_INA219::getBusVoltage_raw()` discards both: it returns
`(value >> 3) * 4`, throwing away bits 2, 1 and 0.

- **CNVR (bit 1, conversion ready)** would have detected the 2026-07-27/28
  frozen-reading defect *directly*. The fix shipped in `007a46b` waits a fixed
  5 ms after `powerSave(false)` and then trusts whatever it reads — it cannot
  distinguish "converted" from "returned the same stale register contents".
- **OVF (bit 0, math overflow)** is set when the Current or Power calculation
  is out of range: *"indicates that current and power data may be meaningless"*
  (SBOS448G §8.6.3.2). We integrate current into the harvest accumulator with
  no such check.

The clear conditions make a CNVR poll unambiguous for our exact access pattern:
`powerSave(false)` writes MODE=`111` → clears CNVR; `powerSave(true)` writes
MODE=`000` → the datasheet **explicitly excepts** power-down from clearing it;
only a *Power*-register read clears it otherwise, and we never read `03h`.

A second reason this matters: the 5 ms wait is correct **only** for the current
12-bit single-sample setting (532 µs, 586 µs max, per channel). Switching
`BADC`/`SADC` to 128-sample averaging makes the requirement **68.10 ms**, and
5 ms would silently return stale data — the same freeze by another route. A
CNVR poll is immune to that change; a fixed delay is not.

### Solution

1. Add a raw 2-byte read of register `02h` (the Adafruit accessor cannot return
   the flags). `variant_probe.h` already does raw `Wire` I²C, so this matches
   the existing style — a small helper in the `.ino` beside the probe.
2. Replace the fixed 5 ms wait with: `powerSave(false)` → poll `02h` until
   CNVR=1 → read. Timeout **10 ms** (8.5× the 1.172 ms worst case for
   shunt+bus), servicing `os_runloop_once()` while polling as the current wait
   does. The poll reads `02h`, which does **not** self-clear CNVR.
3. On timeout, raise a fault. Decide: reuse `DIAG_FAULT_INA219_READ_FAIL`
   (`0x0008`, meaning already "live read bad") or add a distinct bit for
   "present but not converting". Reuse is simpler and needs no decoder change;
   a distinct bit is more diagnostic. **If a new bit is added it must land in
   `diagnostics.h`, the `.ino`, and `decoders/gisebo-05-v7.js` together**, and
   the formatter must be re-uploaded to TTN.
4. Surface OVF as a fault bit. In our configuration OVF should be unreachable
   (shunt clips at ±400 mA before Current can overflow; Power full scale is
   65.5 W against a ≤6.4 W ceiling), so it firing means something structural —
   most plausibly a corrupted Calibration register, which is a live risk because
   `getCurrent_raw()` rewrites `05h` on every single read.
5. Host-test the gate logic in `diagnostics.h`/a new header (pure function of
   "CNVR seen? elapsed?"), keeping the I²C in the `.ino` as the probe does.

### Verification

Host tests for the decision logic; compile; then over the air — a DEV unit must
keep reporting live panel V/I with no new faults, and pulling the INA219's SDA
(or commanding power-down without the wake) must raise the fault rather than
report frozen-but-plausible numbers. Note the failure this replaces was
invisible for a full night, so the exit criterion is the *fault*, not the data.

---

## 19. `ina219.success()` is only checked for the last read

**Status:** **IMPLEMENTED 2026-07-28** — `docs/dev-notes/20260728-1900_tx-fault-latch-and-ina219-success.md`. Pending flash-verify (no-op on a healthy unit by design).
**Complexity:** Low
**Estimated time:** ~1 h
**Sprint:** 07

### Problem

`Adafruit_INA219::success()` returns `_success`, which each accessor
**overwrites** with the result of its own register read. `readAndBufferSensors()`
currently does:

```c
uint16_t busMv = (uint16_t)(ina219.getBusVoltage_V() * 1000.0f);  // sets _success (bus read)
float currentMa = ina219.getCurrent_mA();                          // OVERWRITES it (current read)
g_ina219ReadOk = ina219.success() && (busMv < 20000);
```

So a **failed bus-voltage read followed by a successful current read reports
healthy**, and `busMv` carries whatever the failed read left behind. The check
was introduced deliberately (replacing a `>= 500 mV` floor that would have
false-faulted every night on a dark panel) — the reasoning was right, the
sampling point was wrong.

### Solution / verification

Sample `success()` immediately after **each** accessor and AND the results:

```c
uint16_t busMv = (uint16_t)(ina219.getBusVoltage_V() * 1000.0f);
bool busOk = ina219.success();
float currentMa = ina219.getCurrent_mA();
bool curOk = ina219.success();
g_ina219ReadOk = busOk && curOk && (busMv < 20000);
```

Folds naturally into item 18 (same function, same commit batch) but is worth
doing independently since it is a two-line correctness fix on shipped code.
Verify by compile plus the existing host suite; over the air it is a no-op on a
healthy unit, which is the point.

---

## 20. Probe hardening: check the calibration register after the soft reset

**Status:** **DONE, FLASH-VERIFIED 2026-07-29 10:33** (`0a2dbf9`) — the 32-bit identity still detects the sensor: `mode: SOLAR`, FPort 21, `ina219_config 0x399F`, `ina219_seen: true`. No regression to PRIMARY.
**Complexity:** Low
**Estimated time:** 2–3 h
**Sprint:** 07 (or drop)

### Problem

`probeAttemptFoundIna219()` identifies the part by one 16-bit value: config ==
`0x399F` after the soft reset. That is as much as the INA219 offers — **it has
no manufacturer-ID or die-ID register**, the map ends at `05h` (unlike the
INA226's die ID `0x2260`). So the identification is inherently a 16-bit match,
and any other device at `0x40` that happens to read `0x399F` at register `00h`
would be taken for an INA219.

Separately: the probe ignores the return value of the RST write
(`Wire.endTransmission()`). A NAK is correctly harmless when nothing is there,
but a *partial* failure on a marginal bus leaves the probe reading a stale
config — and since `007a46b` that stale value is **`0x0198`**, not `0x019F`,
because every cycle now ends in `powerSave(true)`.

### Solution / verification

The RST bit *"resets all registers to default values"*, so after a successful
soft reset the **Calibration register `05h` must read `0x0000`**. Reading it
alongside the config turns a 16-bit identity match into a 32-bit one for two
extra I²C bytes. Extend `ProbeResult` with `calValue`/`calRead` and require both
in `probeAttemptFoundIna219()`; add host tests to `test_variant_probe.cpp`
covering "config right, cal wrong" (reject) and the existing cases (unchanged).

**Judgement call, not obviously worth it:** the probe currently works, this
guards a low-probability collision, and every added condition is another way to
*fail* to detect a present sensor — which item 10 correctly identifies as the
worse error (silent 7-day interval) versus a spurious solar detection (loud in
the backend). Decide explicitly; dropping this item is a legitimate outcome.

### Not doing (recorded so it is not re-litigated) — item 20

**Shunt/bus saturation detection.** PG=/1 gives ±40 mV → ±400 mA across the
0.1 Ω shunt, and BRNG=0 gives 16 V; beyond either, the registers simply clip
with **no flag at all**. The panel draws ~30 mA, so there is ~13× headroom and
this is theoretical today. It stops being theoretical the day a larger panel is
fitted — the current reading would peg at 400 mA and the harvest accumulator
would integrate a plausible wrong number. Detection would be
`shunt register == 0x0FA0`. See `docs/ina219-register-reference.md` §4;
revisit only if the panel or shunt changes.

---

## 21. Only one out-of-band frame gets through per cycle

**Status:** **IMPLEMENTED 2026-07-28** — `docs/dev-notes/20260728-2000_first-sample-dt-and-tx-ready-wait.md`. Pending flash-verify: a boot must land all THREE frames in one cycle.
**Complexity:** Low–Medium
**Estimated time:** 3–4 h
**Sprint:** 07

### Problem

Observed on gisebo-05 immediately after the 2026-07-28 flash:

| cycle | data (21) | fault (2) | verbose (3) |
|---|---|---|---|
| boot 14:06:55 | **sent** | deferred | deferred |
| 15:07:00 | n/a | **sent** | deferred |

`loop()` sends data → fault → verbose, each via a separate blocking call. Every
uplink draws a TTN downlink, after which LMIC owes the network a MAC answer and
sets `OP_POLL` — so `LMICJ_isTxPathBusy()` (`OP_POLL | OP_TXDATA | OP_JOINING |
OP_TXRXPEND`) is true and `LMIC_setTxData2()` refuses the *next* frame in the
same cycle. The MAC answers are visible in TTN as the payload-less uplinks that
follow each real frame (`14:07:01/:07/:12/:18`).

The `OP_TXRXPEND` guard at the top of `txFrameAndWait()` does not catch this —
`OP_POLL` is a different bit — so the frame reaches `LMIC_setTxData2()` and is
refused there. The refusal is **caught and reported** (that is the `007a46b`/
`58e4f74` hardening working as designed: the 15:07 fault frame carries
`tx_timeout`, and no 120 s was wasted). But the frame itself is still lost to
that cycle.

**Why it matters more in PROD than it looks here.** In DEV the deferred frame
retries an hour later. In PROD the retry waits a full sleep interval — up to
**7 days** at index 10 — so the once-per-boot diagnostic frame, the one that
says "I am alive, here is my reset cause and state", could be delayed by days
on exactly the unit that most needs to report. The boot diagnostic took 60 min
to arrive here; that is the same defect with a friendly interval.

Note this is a *delay*, not a loss: `bootDiagSent` and `verboseSentOnce` are
only set on success, so nothing is dropped, and in steady state (one frame due
per cycle) everything gets through — which is why it did not show up before.

### Solution

Give the out-of-band path a bounded wait for the MAC exchange to drain instead
of giving up instantly:

1. Before queueing, spin `os_runloop_once()` until `LMIC_queryTxReady()` is true
   or a budget expires (~20–30 s: the MAC ping-pong runs at the ~6 s duty-cycle
   pacing, so 3–5 exchanges fit). `millis()`-based, no `delay()`, exactly like
   the existing waits.
2. Only then call `LMIC_setTxData2()`; keep the return-value check and the
   120 s completion timeout unchanged.
3. Prefer `LMIC_queryTxReady()` over hand-testing opmode bits — it is the
   library's own `! LMICJ_isTxPathBusy()` and will not drift from the internal
   definition the way the `OP_TXRXPEND`-only guard did.
4. Consider whether the budget should be shorter in PROD (awake time is battery)
   — but note the alternative is a multi-day delay, so a 30 s spend is cheap.

### Verification

Over the air: a boot must land **all three** frames (data, fault, verbose)
within one cycle rather than one per cycle. Host-test the "wait for ready, then
give up" decision as a pure function of (ready?, elapsed, budget). The failure
this fixes is invisible in a healthy steady state, so verify at a **boot**, not
in the middle of a run.

---

## 22. The first sample after every boot fabricates a full interval of dt

**Status:** **IMPLEMENTED 2026-07-28** — `docs/dev-notes/20260728-2000_first-sample-dt-and-tx-ready-wait.md`, with six new host cases. Pending flash-verify: a boot's first solar frame must report `harvest_mah` unchanged from the pre-reset value (warm) or 0 (cold).
**Complexity:** Low
**Estimated time:** 1–2 h
**Sprint:** 07

### Problem

`readAndBufferSensors()` feeds the solar policy the interval it *just slept*:

```c
solarPolicy.ingestSample(busMv, currentMa, sleepIntervalSeconds);
```

That is correct for every cycle except the first. `setup()` sets
`sleepIntervalSeconds` from the restored (or default) interval index **before
any sleep has happened**, so the first sample of every boot credits a full
interval of elapsed time that did not elapse.

- **Sun EWMA: negligible.** α = 1 − exp(−3600/86400) = 0.041 at a 1 h interval,
  applied once.
- **Harvest accumulator: material.** A warm reset at index 5 in good sun credits
  `34.5 mA × 1 h ≈ 34 mAh` of charge that never flowed — **more than a typical
  full day's harvest** (7–28 mAh/day at energy balance), injected in one step
  into the metric whose entire purpose is the daily energy-balance trend.

A small instance is already on the wire: the 2026-07-28 14:06:55 cold boot
reported `harvest_mah: 2`, which is exactly `34.5 mA × 300 s` (index 2, the
cold-boot default) — 2.875 mAh, floored to 2 by the accumulator. Harmless at
5 min; the same defect at a restored 60 min index is 12× larger, and `.noinit`
restores `harvest_.totalMah` across a warm reset, so the phantom lands on top of
a real running total rather than on zero.

Applies to **both** reset paths, and PROD is the worse case: the join-failure
`NVIC_SystemReset()` re-enters `setup()` with a restored index.

### Solution

Pass `dt = 0` for the first `ingestSample()` after boot — a static
`firstSampleAfterBoot` flag in the `.ino`, cleared after the first call. Both
primitives already behave correctly:

- `sunEwmaUpdate()` has an explicit `if (dtSeconds == 0) return ewma;` guard,
  already covered by `test_solar_signal.cpp` ("dt=0 leaves the EWMA unchanged").
- `harvestAdd()` computes `currentMa * (0 / 3600.0f)` = 0, so nothing accrues.

So this is a guard in the glue, not a change to any host-tested primitive.

**Why zero rather than the true elapsed time:** the device genuinely *was* off
for some interval (the 15-min PROD join-failure sleep, a flash, a power cut) and
the panel may well have been charging through it — so zero under-counts. But the
duration is not knowable from `millis()`, which does not advance through deep
sleep or across a reset. Under-counting a quantity we cannot measure beats
fabricating one, and a fabricated value is indistinguishable from real harvest
downstream. If precision ever matters, the RTC could supply it
(`persist.rtcEpoch` → `rtc.getEpoch()` when `clockValid`) — deliberately not
done here, as it adds a clock dependency to a path that must work without one.

### Verification

Host test: `ingestSample(busMv, currentMa, 0)` leaves both `ewma_` and
`harvest_.totalMah` unchanged (the EWMA half already exists; add the harvest
half and the combined call). Over the air: a boot's first solar frame must
report `harvest_mah` **equal to the pre-reset value** (warm reset) or **0**
(cold boot), rather than the pre-reset value plus an interval's worth.

---

## 23. The TX-fault latch and the diagnostic rate limiter deadlock

**Status:** **IMPLEMENTED 2026-07-28** — `docs/dev-notes/20260728-1900_tx-fault-latch-and-ina219-success.md`. Pending flash-verify: after a refused frame, `tx_timeout` must ride exactly ONE verbose frame, not 24 h of them.
**Complexity:** Low
**Estimated time:** 1–2 h
**Sprint:** 07

### Problem

`g_txFaultPending` means "an uplink failed and no diagnostic frame has reported
it yet", and it is cleared in exactly one place — after `sendDiagFrame()`
succeeds. But `diagShouldSend()` will not send a frame for a fault bit it has
already reported (`unreported = faults & ~lastSentFaults`), and the periodic
re-alert is rate-limited to `DIAG_MIN_RESEND_SECONDS` (24 h). So once the bit
has been reported once, the only thing that can clear the latch is suppressed
by the very fact that it was reported.

Observed sequence:

| time | event |
|---|---|
| 14:07 | boot-cycle fault + verbose frames refused (`OP_POLL`, item 21) → latch set |
| 15:07 | diagnostic frame sent, carries `tx_timeout`, latch cleared… then the verbose frame is refused → **latch set again** |
| 16:07, 17:07, 18:07 | verbose frames succeed, each reporting `tx_timeout` / `healthy: false` |

Nothing had failed since 15:07 — the verbose frames were going out cleanly on an
exact hourly cadence — yet the device reported itself unhealthy for hours, and
would have gone on doing so until the 24 h re-alert at ~15:07 the next day.

Two costs:

1. **A stale bit reads as a live fault.** `healthy: false` on a healthy device is
   noise on the bench and a false alarm in the field.
2. **Recurrence is invisible.** A single old failure and a failure every hour
   produce identical telemetry, because the bit is latched and the re-report is
   suppressed. That is the opposite of what the latch was added for.

### Solution

The verbose frame already carries the full fault bitmap (`gatherVerbose()` calls
`diagComputeFaults()`), so when it transmits, the fault **has** been reported
over the air. Clear the latch there too:

```c
if (txFrameAndWait(VERBOSE_FPORT_DEV, payload, DIAG_VERBOSE_LEN)) {
  lastVerboseMillis = millis();
  verboseSentOnce = true;
  if (v.faults & DIAG_FAULT_TX_TIMEOUT) g_txFaultPending = false;  // reported on the wire
}
```

Ordering is already safe: `gatherVerbose()` snapshots the faults *before* the
TX, so a frame that succeeds provably carried the bit, and a frame that fails
sets the latch again through `txFrameAndWait()`.

DEV-only in effect — the verbose frame does not exist in PROD, so PROD keeps
today's behaviour (cleared only by a diagnostic frame). That is the right
asymmetry: DEV gets per-occurrence resolution for bench work, PROD keeps the
once-per-day spam limit that protects the duty cycle and the battery.

**Considered and not chosen:** widening `DIAG_FAULT_TX_TIMEOUT` into a small
counter (failures since last report). It would distinguish recurrence in PROD
too, but costs payload bits and a schema bump on both diagnostic frames for a
fault that should be rare. Revisit only if PROD units start reporting it.

**Not a bug, deliberately:** `diagMarkSent()` latching the bit into
`persist.diagLastSentFaults` so a persistent fault re-alerts at most daily is
the intended spam-proofing (item's origin: `20260726-1905_diagnostic-error-uplink.md`).
This item does not change that.

### Verification

Host test: a verbose send with `tx_timeout` set clears the pending flag; a failed
verbose send leaves it set. Over the air: after a refused frame, `tx_timeout`
must appear on exactly **one** verbose frame and be absent from the next, rather
than persisting for 24 h.

---

## 24. `clarity` is gated on the device clock, which it does not use

**Status:** **(a) DONE 2026-07-28**; **(b) DONE 2026-07-29, FLASH-VERIFIED 2026-07-30 10:36** (`40e3331`) — schema-2 verbose frames gate clarity on uptime >= 24 h (`clarity_converging` while younger). Observed flipping exactly at the gate: the 09:36 frame at `uptime_s 83024` reported `clarity: null, clarity_converging: true`; the 10:36 frame at `uptime_s 86637` reported `clarity: 0.608, clarity_converging: false`, matching `sun_ewma 0.412 / expected_daylight_fraction 0.678` to the third decimal. Data frames stay ungated by design: a stateless formatter cannot gate a payload with no age field; backend gating folded into item 10.
**Complexity:** Low (part a) / Medium (part b)
**Estimated time:** 1–2 h (a), folds into the verbose-frame work (b)
**Sprint:** 07

### Problem (a) — the gate is simply wrong

`decoders/gisebo-05-v7.js`:

```js
if (data.clock_valid && input.recvTime) {
  const frac = expectedDaylightFraction(new Date(input.recvTime), SITE_LATITUDE_DEG);
  data.clarity = frac > 0 ? Number((data.sun_ewma / frac).toFixed(3)) : null;
} else {
  data.clarity = null;
}
```

Neither operand needs the device's RTC:

- `expectedDaylightFraction()` takes **`input.recvTime`** — the network server's
  own receive timestamp, which The Things Stack attaches to every uplink and
  passes to the formatter as a `Date` — plus `SITE_LATITUDE_DEG`, a constant.
- `sun_ewma` is computed on-device from `dt = sleepIntervalSeconds`, not from
  the RTC (`solar_signal.h`: `alpha = 1 - exp(-dt / TAU)`).

So `data.clock_valid` — a flag about the *device's* clock — suppresses a
calculation made entirely from server-side time and a compile-time constant.
Effect: `clarity` is `null` for the whole early life of any device, and forever
on any unit whose `DeviceTimeReq` never lands. Fix is to drop `data.clock_valid`
from the condition and keep `input.recvTime`.

### Problem (b) — the gate the code actually needs, and does not have

The likely *intent* was to suppress a meaningless clarity figure, and that
concern is real — but `clock_valid` does not measure it. `clarity` is
meaningless until the **EWMA has converged**, and nothing checks that.

Live example from gisebo-05 right now: `clock_valid: true`, `sun_ewma: 0.157`,
and `expected_daylight_fraction ≈ 0.68` for 2026-07-28 at 57.81°N — giving
`clarity ≈ 0.23`, which reads as "panel 77% obscured" when the truth is "booted
four hours ago and the EWMA has taken four samples". The current gate does not
catch this, and removing it does not make it worse — the case is unguarded
either way.

A correct convergence gate needs to know how long the EWMA has been
accumulating, which **the payload does not carry**. That is the same missing
field as the proposed verbose-frame `uptime` / `cycle count` (see the
2026-07-28 discussion of DEV status uplinks): with it, the decoder could
suppress or flag `clarity` until roughly one time constant (24 h) has elapsed
since the last cold boot. Until then, `expected_daylight_fraction` is already
emitted alongside, so the backend can at least judge for itself.

### Solution / verification

1. **(a)** Remove `data.clock_valid &&` from the condition. Re-upload the
   formatter to TTN — the repo file must stay byte-identical to what is pasted
   into the console (`test/harness.js` loads it as text precisely so the two
   cannot drift).
2. **Update the existing test, which asserts the current behaviour.**
   `test/run_v7.js` has a case `"clock invalid: clarity is null"` (the block
   headed *v7 decoder — clock invalid -> clarity null*). It must become
   "clock invalid but recvTime present → clarity is computed". Note this is a
   test that was correct against the old intent and is wrong against the new
   one — change it deliberately, do not delete it.
3. **(b)** When the verbose frame gains an uptime/cycle-count field, add the
   convergence gate and a test for "EWMA too young → clarity suppressed".

### Why this is worth doing at all

`clarity = sun_ewma / expected_daylight_fraction` is the only signal that
separates "short winter day" from "snow, leaves or shade on the panel" — a fault
a bare EWMA hides completely, and one that a field unit cannot otherwise report.
Leaving it `null` for a device's whole early life disables exactly the diagnostic
that early life most needs.

---

## 25. Verbose frame v2: guaranteed hourly, uptime/cycle count, DEV panel sub-sampling

**Status:** **DONE, FLASH-VERIFIED 2026-07-29 10:33** — first schema-2 frame decoded end to end: 34 bytes, `diag_schema: 2`, `uptime_s: 40`, `cycle_count: 1`, `ram_count/uplink_counter` present, panel profile populated. Hourly cadence and a multi-sample profile spread confirm on the next frames.
**Complexity:** Medium
**Estimated time:** 5–7 h
**Sprint:** 07

### Problem

Three gaps in the existing FPort 3 verbose frame, all found the hard way this
week:

1. **Cadence is `max(1 h, wake interval)`, not hourly.** The frame is emitted
   from the wake cycle, so at winter/degraded intervals (6 h, 12 h, …) status
   stretches exactly when bench observation wants it most. DEV never
   deep-sleeps, so a truly hourly cadence is free.
2. **The device never reports its own sense of time.** Cadence can only be
   inferred from TTN receive timestamps, which is precisely the ambiguity that
   made the overnight TX-stall analysis hard ("sleeping too long, or
   transmitting too little?" took gap arithmetic against the harvest counter to
   answer; uptime + a cycle count would have answered it in one glance). It is
   also the missing input for **24b**: clarity is meaningless until the EWMA has
   converged, and nothing on the wire says how long it has been accumulating.
3. **Harvest rests on one ~532 µs sample per hour.** The 15:07→16:07 jump
   (2 → 129 mAh, implying an uncorroborated ~105 mA sample) showed how fragile
   that extrapolation is. In DEV the device is awake the whole hour; sampling
   the panel every 1–2 min costs nothing and yields min/mean/max — a real
   charging profile, and a direct measurement of the extrapolation error that
   item 15's harvest error bar needs.

### Solution sketch (schema v2, DEV-only fields appended)

- **Guaranteed hourly:** also call `evaluateAndMaybeSendVerbose()` from inside
  the DEV sleep loop (it already runs `os_runloop_once()` + the clock feed;
  the `verboseShouldSend()` millis gate already exists). Linear flow, no OS
  jobs; must go through `waitForTxReady()` like every other frame. PROD path
  untouched — the whole feature stays behind `runMode == 1`.
- **New fields (bump `DIAG_VERBOSE_SCHEMA` to 2, extend `DIAG_VERBOSE_LEN`):**
  uptime (u32 s), wake-cycle count (u16), `ramCount`/`uplinkCounter` (packed
  byte), and panel-current min/mean/max at 0.5 mA/LSB (3 bytes) + bus-voltage
  min/max at 30 mV/LSB (2 bytes) accumulated by the sleep-loop sampler and
  reset after each verbose TX. ~34 bytes total — inside the 51-byte SF12 limit
  with margin.
- **EWMA age for 24b:** report uptime; after a warm reset the EWMA is restored
  and thus OLDER than uptime, so the decoder's convergence gate under-estimates
  age and suppresses clarity a little too long — conservative, acceptable, and
  avoids a persist-layout bump. (Exact alternative: a persisted
  `ewmaAgeSeconds`; do not build it unless the conservative gate annoys.)
- **Sub-sampling and powerSave:** each sleep-loop sample must wake the INA219
  and re-sleep it (or, at 1–2 min spacing in DEV, just leave it awake between
  samples — 0.7 mA is irrelevant on the bench; decide and document). Gate on
  CNVR once item 18 lands.
- Land `diagnostics.h` + `.ino` + `decoders/gisebo-05-v7.js` together, re-upload
  the formatter, keep schema-1 decoding for old captures (the decoder keys on
  byte 0).
- Then implement **24b** in the decoder: suppress or flag clarity while
  uptime < ~1 time constant (24 h) since cold boot; host-test both sides of the
  gate.

### Verification

Host tests for the new encode fields and the min/mean/max accumulator (pure
logic → header). Over the air: verbose frames at 60-minute spacing while the
wake interval is 6 h (force with a bench battery/temperature); `cycle_count`
advancing by 1 per wake; panel min < mean < max on a partly cloudy day;
`clarity` null/flagged for the first 24 h after a cold boot and computed after.

---

## 26. `sunPresent()`'s absolute 3000 mV threshold is structurally unreachable at night

**Status:** **DONE, FLASH-VERIFIED 2026-07-30** (`58c9572`, flashed 2026-07-29 10:33). The full night settles it: `sun_ewma` fell monotonically across **8 consecutive dark frames** (0.341 → 0.243, 21:34 → 04:35, every step within the 8-bit wire quantum of the `exp(-1/24)` model), reached its trough at the last pre-sunrise wake, then **rose monotonically across 4 dawn frames** (0.243 → 0.361). Both arms verifiably failed in the dark: 0.0 mA against the 1 mA floor, bus 166–169 mV *below* battery against the +150 mV relative test — the same back-feed input the old absolute threshold called "sun" all night. `bonus_active` stayed false throughout. Capture: `ttn-captures/gisebo05-ttn-20260730-morning.jsonl`.

**Threshold validation worth keeping:** dawn was detected at 05:36 by the current arm reading **1.1 mA** against `SUN_CURRENT_MA` = 1.0, while the relative arm was still failing at −167 mV. A 2 mA floor would have missed that wake and delayed recovery by an hour, so the 1 mA choice is confirmed by measurement, not just by noise-floor argument.
**Complexity:** Medium (signature change through a host-tested header)
**Estimated time:** 3–5 h
**Sprint:** 07 — **should land before the queued flash**, or the EWMA re-poisons
within ~13 h of the reboot.

### Problem

The night half of the sun signal executed for the first time on 2026-07-28/29 —
live INA219 readings, panel current a true 0 mA all night — and the design
assumption failed: **the bus voltage never collapsed**. It sat at
**battery − ~180 mV ≈ 3.57–3.61 V** from dusk to dawn (back-fed pack voltage on
the charger input node), far above `SUN_PRESENT_MV` (3000 mV). `sunPresent()`
returned true through eight hours of total darkness and the EWMA **rose
monotonically 0.255 → 0.529 overnight** — the exact poisoning signature of the
frozen-sensor defect, now from an honest sensor.

Structural, not incidental: the night bus tracks the pack, the pack lives in
3.4–4.2 V (the Feather browns out below ~3.4), so night bus ≈ 3.2–4.0 V — the
3.0 V threshold cannot be reached while the device is alive. Consequences chain
exactly as before: the EWMA converges on 1.0 (day AND night count as sun), the
bonus latches around 0.55 (**projected ~10:00 on 2026-07-29**) and can never
release (0.45 unreachable from an always-1 input). The `voltage_offset == 0`
second gate holds it un-applied while the pack sits below the 3.90 V improve
edge — the same bounded-oscillation containment as before, not harmlessness.

### Why the two obvious fixes are both wrong (measured, not argued)

The overnight bus-vs-battery table:

| when | bus − batt | panel mA | truth |
|---|---|---|---|
| 16:07 (sun) | **+122 mV** | 22 | sun |
| 17:07 | +9 mV | 13.8 | sun |
| **18:07 (low sun)** | **−90 mV** | **11.6** | **sun** |
| 19:07 (dusk) | −169 mV | 2.4 | sun |
| 21:07–05:08 (night) | **−177…−186 mV** | **0** | dark |
| 06:08–07:08 (dawn) | −181…−179 mV | 1–2 | sun |
| boot 14:05 (terminated) | **+940 mV** (5.10 vs 4.16) | 0 | sun |

- **Raising the absolute threshold above 4.2 V** misses every charging
  operating point ever observed (3.57–3.99 V bus while current flows).
- **A pure relative test (`busMv > batteryMv + margin`)** fails the measured
  low-light rows: at 18:07 the panel pushed 11.6 mA with the bus **90 mV
  below** the battery, and dawn charging runs at −180 mV. Any positive margin
  calls those "dark".

### Solution: a two-arm predicate — either arm proves sun

```c
sunPresent = (currentMa >= SUN_CURRENT_MA)                    // ~1 mA
          || (busMv > batteryMv + SUN_BUS_ABOVE_BATT_MV);     // ~150 mV
```

- **Current arm** covers every charging case, including the measured
  bus-below-battery low-light rows (11.6, 2.4, 1–2 mA all ≥ 1).
- **Relative arm** covers the one case the current arm cannot: charge
  termination — full pack in bright sun, 0 mA by design, bus at panel Voc
  (+940 mV observed). This is the founding observation of the voltage-keyed
  design (`solar_signal.h` header) and it stays covered.
- **Night**: 0 mA and −180 mV fails both arms. Margin 150 mV sits between the
  tightest observed night offset (−169 mV at dusk) and the termination case
  (+940 mV) with room; the current arm makes the +122/+9 mV charging rows
  irrelevant to the margin choice.

Plumbing: `sunPresent()` gains the battery argument →
`ingestSample(busMv, currentMa, batteryMv, dtSeconds)` → the `.ino` passes the
already-sampled vbat. `solar_signal.h` + `policy_solar.h` + their host tests
change together; **payload and decoder unchanged** (firmware-only). The
`SOLAR_NO_INA219` bench path has no current reading — its arm is always false,
leaving the relative test only; note that at the call site.

Also fold in (same header, same flash): re-derive the historical claim that
Fall/Spring peaks at ~0.52 — the hysteresis band (0.45/0.55) was tuned for a
0/1 duty-cycle input, which the two-arm predicate preserves, so the band should
hold; state the check rather than assuming it.

### Verification

Host tests: all four measured quadrants (charging bus-above, charging
bus-below, terminated 0 mA + Voc, night 0 mA + backfeed) plus the margins.
Over the air, the same test that exposed this: after a flash, `sun_ewma` must
**decrease between consecutive night frames** — the one behaviour this device
has never shown. Watch the first night after deployment of the fix.
