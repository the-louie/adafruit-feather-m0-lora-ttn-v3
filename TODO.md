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

**Historical impact:** every PROD reading taken by a v6 unit is time-shifted one interval late. `gisebo-01` is v6 and has been since at least f_cnt 376. `gisebo-04` is still on the 8-byte V5 firmware, whose source is not in this repo — **whether V5 has the same defect is unknown and must be established** (item 13).

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

Deployed units need a reflash. An un-reflashed unit is not corrupted — its counter just still means "wake count" — but its decoder must stay pinned at `FIRMWARE_VERSION = 6` until it is.

The {1, 7, 13} signature is a useful fleet-tracking tool in its own right: any unit still emitting it has not been reflashed.

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

**Status:** Not started — blocked on item 13 (harness targets the wrong decoder without it), then items 2, 8, 9
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

**This is live in production now.** `gisebo-04` reads 5.233 V and is drifting toward the 5.00 V edge; when it arrives it will dither. The season machine got 1 °C of hysteresis for exactly this reason and the voltage ladder never did.

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

**One combined decoder, not one per variant.** The live decoder already branches on length for 8 vs 9 bytes, so 15-byte extends an existing pattern and bytes 0–8 parsing stays in one place. Two files would drift — exactly how `ttn-decoder-v6.js` drifted from what actually runs (item 13).

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

- **Supercap voltage rating.** Across a 1S pack it sits on a 3.0–4.2 V rail; standard supercaps are 2.7 V rated. Needs a 5.5 V module (two in series, half the capacitance) or explicit part selection. Its leakage (µA to tens of µA) counts against the sleep budget alongside the 15 µA INA219.
- **Over-discharge protection.** The Feather browns out near 3.4 V but keeps leaking tens of µA afterward, walking a li-ion cell down to destruction over a long dark spell. Firmware cannot help once it is off. Protected cells or a PCM.
### Accepted, not open

- **Quiescent draw.** Accepted for the solar variant on the grounds that solar makes it affordable. **That reasoning does not extend to the primary variant.** From the brief's own figures a 12× interval change (30 min → 6 h) moves consumption 31%, which backs out to ~6.9 mAh/day (~290 µA) drawn regardless of wake count — consistent with a stock Feather M0's regulator, charger, and USB leak. That means ~a year on 4×AA and nearer seven months on 2× CR123A, and the interval ladder saves under 4%. If multi-year life on primaries is ever a requirement, the answer is quiescent-current surgery, not interval tuning.

### Notes — accepted risks

**Subzero charging** is accepted: cells are consumable on a 5–10 year cycle. The rationale holds better than it first appears — plating is strongly rate-dependent and the charge rate is panel-limited to ~30 mA into ~6000 mAh, about C/200. The MCP73831 will ask for 100 mA and never get it. C/200 into a cold cell is far gentler than the C/2-and-up regimes the "never charge below freezing" rule addresses. Consequence: replacement timing becomes a telemetry problem (item 10).

**The floor rests on an unmeasured number.** The brief's figures imply ~0.075 mAh per wake; a model of the wake (750 ms conversion plus an occasional 3 s TX) suggests closer to 0.002 mAh. At 5-minute sampling that is 28 mAh/day versus 7.6 mAh/day. The harvest accumulator will settle it from field data within a few months. Until then index 2 is defensible mainly because the healthy-battery gate self-corrects.

---

## 13. `ttn-decoder-v6.js` is not what runs in production

**Status:** Not started — **blocks item 3; do early**
**Complexity:** Low
**Estimated time:** 3–4 h
**Sprint:** 01

### Problem — found 2026-07-16 from `docs/dev-notes/real-world-data__20260716.json`

The decoder running in the TTN console is **not** the one in this repo. Evidence from `decoded_payload`:

- It reports `"version": 5`; `ttn-decoder-v6.js:32` sets `data.version = 6`.
- It emits `entries: [{temperature, timestamp}, ...]` with **extrapolated per-sample timestamps**; the repo file emits a flat `temperatures` array with no timestamps.
- It **omits** null slots entirely (a 4-sample batch yields 4 entries); the repo file pushes `null` for byte 250.
- It correctly decodes **both** 8-byte and 9-byte payloads, branching on length; the repo file rejects anything under 9 bytes outright (`:15`).

So the deployed decoder is strictly more capable than the repo's, and the repo's is a stale artifact of unknown provenance. This matters immediately: item 3 plans to validate test vectors against `ttn-decoder-v6.js`, which would validate **an artifact nobody runs**.

### Solution

1. Export the live decoder from the TTN console and commit it as the source of truth.
2. Diff against `ttn-decoder-v6.js` and establish which is intended. Reconcile or delete the repo copy — do not leave two.
3. Only then proceed with item 3's vectors, and item 9's v7 work, against the real decoder.

### Notes

The timestamp extrapolation in the live decoder is exactly the consumer of byte 0 that the design assumes — and it is also the thing the item 1 lag silently corrupts, since it extrapolates from an uplink timestamp that no longer corresponds to when the sample was taken.

---

## 14. The fleet runs two firmware versions, and V5's source is not in this repo

**Status:** Not started
**Complexity:** Low (investigation)
**Estimated time:** 2–3 h
**Sprint:** 01

### Problem — found 2026-07-16

- `gisebo-01` sends **9 bytes** — the v6 protocol in this repo.
- `gisebo-04` sends **8 bytes** — the pre-interval-byte V5 protocol, whose firmware source **is not in this repo**.

`gisebo-04`'s battery reads 5.233 V and is drifting toward the 5.0 V `VOLTAGE_HEALTHY_V` threshold; when it crosses, its interval moves from base to base+1. `gisebo-01` reads 5.768 V, water 16.8–19.0 °C, interval index 4 — which is exactly Summer base (≥16 °C) + `voltage_offset` 0 (≥5.0 V). **The interval algorithm is confirmed working in production.**

### Solution

- Establish what firmware `gisebo-04` is running and whether that source exists anywhere. If it does not, that unit cannot be maintained or reasoned about — reflashing it to current v6 is the only way back to a known state.
- Determine whether V5 shares the item 1 `idle(750)` defect. If its source is gone, this is unanswerable and the unit's entire history is uninterpretable.
- Decide the fleet reflash plan: both units to one firmware version, sequenced against the item 2 decoder change.

### Notes

Two units, two protocols, one decoder, and one of the two firmwares has no source. Every item in this backlog that says "deployed units need a reflash" depends on resolving this first.

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
