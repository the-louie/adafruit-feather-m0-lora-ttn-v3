---
name: master-plan
description: Enforces project intentions and objectives to reduce complexity, prioritize reliance.
---

# Master plan

## When to apply

Apply this skill when:

- Creating or reviewing plans
- Writing or editing code in the repository
- Reviewing code or pull requests
- User asks about project ideas or philosophy

## Foundational ideas and philosophy

### Eradicate FlashStorage Entirely
The SAMD21 microprocessor retains its RAM during LowPower.deepSleep(). Writing to Flash consumes power, wears out the memory, and interrupts the SPI bus which the LoRa radio relies on. All session data (LMIC.seqnoUp) and temperature arrays simply live in volatile RAM.

### Move Hardware Strapping to Pin 11
Pin 13 is hardwired to the onboard Red LED. Using INPUT_PULLUP on Pin 13 causes electricity to bleed continuously through the LED to ground, draining your battery during deep sleep. Pin 11 is used for mode strapping: LOW (tied to GND) = DEV, HIGH (floating) = PROD.

### Repurpose Pin 13 for Visual Debugging
Pin 13 is used exclusively as an output LED: on when the device is actively transmitting, off before deep sleep; also 1s on at start and 5 rapid blinks on join accept.

### The Commissioning vs. Operational Split
Trying to manage deep sleep while the device is joining causes timing collisions. If the device has not joined TTN (LMIC.devaddr == 0), the loop() simply spins os_runloop_once(); continuously. No sleeping. Once joined, we drop into the Operational sleep cycle.

### Eradicate Application OS Jobs
MCCI LMIC uses OS jobs for its internal MAC timings. When the application injects its own OS jobs to wake up and read sensors, it clogs the queue. The application logic becomes purely linear: wake up -> read sensor -> format payload -> LMIC_setTxData2() -> wait for EV_TXCOMPLETE -> sleep.

### Respect LMIC Duty Cycle
The application respects LMIC duty cycle and does not override `globalDutyAvail` or `bands[].avail`. After long deep sleep the first transmission may be delayed until LMIC permits it.

### Synchronous TX Waiting
If we go to sleep immediately after queuing a packet, the radio turns off before transmitting. We introduce a volatile boolean flag txComplete. We call LMIC_setTxData2(), and then use a while(!txComplete) { os_runloop_once(); } loop to keep the CPU awake until TTN confirms the transmission is finished.

### Unified Intervals and FPort by Mode
- Sleep interval is derived from a fixed index table (currentIntervalIndex 0–10). The interval index is chosen by a temperature- and battery-controlled algorithm: seasonal baseline (Summer / Fall–Spring / Winter) from water temperature with 1°C hysteresis, plus battery penalty steps; interval is updated only after a successful uplink. Initial index 2 = 5 min; thereafter each TX-complete applies the next interval from the algorithm. The sensor always sends the current interval in byte 0 so the backend can perform time extrapolation when the interval changes mid-history.
- FPort is set from runMode in setup(): 10 = PROD, 20 = DEV. Backend distinguishes mode by FPort, not a payload byte.
- **The 4-bit field is an UPLINK counter, not a wake counter, and it is NOT reboot detection.** An earlier version of this rule said "4-bit sequence (wakeCounter 0..15) in payload for reboot detection". That was wrong on both counts and is the single most expensive error in this project's history — see below.
- The strap sets only runMode (0 = PROD, 1 = DEV). The only behavioral differences by mode:
  - **USB/Serial:** PROD never inits Serial and detaches USB; DEV inits Serial for logging.
  - **Join timeout:** PROD only: after 3 min without join, deep sleep 15 min then NVIC_SystemReset.
  - **Sleep:** PROD uses LowPower.deepSleep; DEV uses a busy-wait (USB active so deep sleep not used).

### The fast-flush fires once per join — enforce it with a flag, never a counter

`wakeCounter == 1` was intended as "the first uplink after joining". It is not. The counter is 4-bit, so it **wraps to 1 every 16 wakes** and re-fires with a partial batch. Confirmed from 139 production uplinks across both devices: the sequence takes only **{1, 7, 13}**, and seq 1 always carries exactly 4 samples. Inter-uplink gaps are only ever 4x or 6x the interval — the same defect visible in timing alone.

**A third of every uplink ever sent** carries a short batch and two dead bytes.

Use an explicit `bool firstUplinkAfterJoin`, set on `EV_JOINED`, cleared after the first successful TX. Never infer "first" from a wrapping counter.

### The 4-bit field is an uplink counter — reboot detection was always free

`rebootDetected = (sequence === 0)` never fired once. It cannot: a free-running 4-bit counter cannot distinguish a reboot from a wraparound **in principle**, and with `batchTarget = 6` the value never reaches 0 anyway.

The 4 bits now count **successful uplinks** — consecutive uplinks differ by exactly 1, so a gap is a dropped message, a repeat is a TX retry. That is a use the field can actually serve.

**Reboot detection was in the metadata the whole time.** `LMIC_reset()` clears `seqnoUp`, so every reboot restarts `f_cnt` — visible in TTN, costing zero payload bytes. Paired with `.noinit` preserving the uplink counter, the two distinguish cold boot (both reset) from soft reset (f_cnt resets, counter continues) from a dropped uplink (f_cnt continues, counter gaps).

### Hysteresis on every threshold an ADC or an EWMA feeds

The season machine has 1 C hysteresis. **The voltage ladder never did, and it needs it**: a single SAMD21 ADC sample carries ~±19 mV of noise (6.45 mV LSB through the A7 divider), so a pack near a band edge flips `voltage_offset` every wake. On the solar variant that edge gates the solar bonus, swinging the interval 5 min <-> 30 min wake to wake.

Rule: **degrade at the nominal edge, improve at nominal + 50 mV.** React promptly to a failing pack, recover reluctantly. Average 16 ADC samples as well — attack the noise at source, absorb the rest with the band.

Same principle, different signal: the solar EWMA engages at 0.55 and releases at 0.45. A bare 0.5 would flap, because the EWMA ripples ±0.11 daily against its 24 h time constant.

**Discriminate — do not blanket-apply.** A threshold feeding an integrator (`sun_present` -> EWMA) needs **no** hysteresis: dithering at the edge contributes fractionally and correctly, and a band there would bias the very fraction being measured.

### `.noinit` is not a FlashStorage violation

State that must survive `NVIC_SystemReset()` lives in a `.noinit` struct guarded by magic + layout version + **CRC**. A soft reset does not physically clear SRAM; only the C runtime zeroes `.bss`. So this costs zero flash writes and honours "Eradicate FlashStorage Entirely" rather than bending it.

The CRC is not optional: magic and version catch layout changes and clean cold boots, but a brief power interruption decays RAM *partially* — long enough to corrupt the body, short enough to leave a 32-bit magic word standing.

**`dataBuffer` and `ramCount` are deliberately NOT preserved.** The decoder extrapolates sample timestamps assuming uniform spacing at byte 0's interval; a buffer straddling a reset carries a multi-interval hole byte 0 cannot express. Losing six samples beats emitting confidently mis-timestamped data.

### Refined Protocol Payload (9-byte uplink with dynamic interval)
- Byte 0: Interval index (0–10). Same interval applies to all measurements in the message. Chosen by firmware from temperature and battery; backend uses it for correct time extrapolation. Index 0 = unused; 1–10 = 1, 5, 15, 30, 60, 120, 360, 720, 1440, 10080 minutes.
- Bytes 1–2: 12-bit battery offset from 3000 mV (0 = 3.0 V, 4095 = 7.095 V) plus 4-bit sequence in low nibble of byte 2.
- Bytes 3–8: Six 1-byte temperatures; 0 = -10°C, 200 = +30°C; 250 = no value, 251 = too cold, 252 = too warm. Mode is indicated by FPort (10/20), not a payload byte.

### Robust SPI Boot Sequencing
The RFM95 needs time to stabilize its voltage before receiving SPI commands. Call delay(5000) and SPI.begin() at the absolute top of setup() before touching the LMIC libraries.


### Two power variants behind PowerPolicy (v7)

The firmware now selects a power variant at boot by probing for an INA219 (present -> solar li-ion, absent -> primary 6 V pack). Both sit behind a virtual PowerPolicy and SHARE the season machine (season.h) and the voltage-band hysteresis (power_policy.h). One binary for every board; the probe, not a build flag, decides.

Solar specifics: keys on panel BUS VOLTAGE not current (current collapses to 0 on a full pack in sun); a fixed 2-step interval bonus gated on BOTH a healthy pack AND a latched sun EWMA, so the two signals cannot fight and the loop self-corrects; a time-based EWMA (decays against real elapsed seconds, not wake count, or it hunts); harvest accumulator. Day length / clarity is DECODER-ONLY -- firmware never computes it, so it stays one-binary.
