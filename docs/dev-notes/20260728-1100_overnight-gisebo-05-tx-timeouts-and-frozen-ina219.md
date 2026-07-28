# gisebo-05, first full night on the bench: two new defects, one fix confirmed

Date: 2026-07-28 11:00 CEST
Capture: `ttn-captures/gisebo05-ttn-20260728-0853Z-last24h.jsonl` (TTN Storage
API, `?last=24h`, fetched 08:53 UTC). 35 records, 07-27 19:39:58 → 07-28
09:21:49 CEST. Analysis script: `ttn-captures/analyze-night.js`.

Everything below is decoded with the **repo** decoder (`decoders/gisebo-05-v7.js`),
not with TTN's `decoded_payload`, and cross-checked against it.

## Summary

| | verdict |
|---|---|
| INA219 warm-reset probe fix (`fe7b533`) | **confirmed working over the air** |
| Uplink transmission | **~2 of every 3 cycles transmit nothing** (new defect) |
| INA219 panel readings | **frozen after the first read of each boot** (new defect) |
| Wake/sample cadence | correct — every ~60 min as configured |
| Radio link | healthy all night, RSSI −55…−79, SNR 9.2–14.2, SF7 throughout |
| Gateway power outage (~21–22 CET) | **no uplinks lost**; only a clock-skew artefact |
| `.noinit` across reset (TODO 17) | still not surviving — `boot_counter` == 1 at every boot |

## 1. Confirmed: the INA219 soft-reset probe fix works

The evening's mode flip-flop is fully explained and is *not* an open problem.

| time | reset cause | probe config | variant | interval |
|---|---|---|---|---|
| 19:39 | `0x40` system (post-flash `-R`) | `0x399F` | SOLAR | 5 min |
| 20:16, 20:23 | `0x10` external (RST pin) | **`0x019F`** | **PRIMARY** | **120 min** |
| 20:53 | `0x01` power-on (re-flash) | `0x399F` | SOLAR | 60 min |
| 20:57 | `0x10` external (RST pin) | `0x399F` | SOLAR | 60 min |

The 20:16/20:23 boots are the A1 catastrophe caught on the wire: an RST-pin
reset left the INA219 holding `0x019F`, the probe read "absent", the unit booted
PRIMARY, and on the PRIMARY bands a 3.755 V li-ion pack scored `voltage_offset`
2 → interval index 6 → **120 min**. Exactly the failure `fe7b533` was written
for.

After the 20:53 re-flash, the **20:57 RST-pin reset produced `0x399F` and SOLAR**.
Same stimulus, opposite outcome, so the soft-reset is doing its job. Nothing
after 20:57 has misdetected.

## 2. New defect: most cycles transmit nothing (silent TX timeouts)

`f_cnt` runs **0→12 with no gaps** from 20:57 to 09:21, so the network received
every frame the device sent — the device only ever transmitted 13 frames in
12.5 hours. Yet the device was awake and sampling the whole time.

**The sampling cadence is correct.** Two independent proofs:

- `harvest_mah` advances by exactly **+16 mAh per cycle** (15.9 mA × 3600 s):
  17 → 64 (3 cycles) → 128 (4) → 192 (4). The harvest accumulator is an exact
  cycle counter, and it says cycles run hourly.
- The 6-sample batch in the 05:15 uplink (21.0, 20.4, 20.0, 19.6, 19.6, 19.6 °C,
  oldest first) interleaves with the verbose frames' `surface_temp`: the 5th
  entry (20.4) matches the 01:01 verbose reading of 20.43 °C, and the newest
  (19.6) matches the 05:15 reading of 19.56 °C. Samples are ~63 min apart.

So the wakes happen; the **transmissions are being dropped**. Reconstructed:

```
C0  20:57:04  data + fault + verbose   sent
C1  21:57:23  verbose                  sent
C2  ~22:57    verbose                  LOST
C3  ~23:59    verbose                  LOST
C4  01:01:29  verbose                  sent
C5  ~02:01    verbose                  LOST
C6  ~03:03    data + diag + verbose    LOST  (batch hit 6 here)
C7  ~04:09    data + diag + verbose    LOST
C8  05:15:35  data + verbose           sent
C9  ~06:15    verbose                  LOST
C10 ~07:17    verbose                  LOST
C11 ~08:19    verbose                  LOST
C12 09:21:49  verbose                  sent
C13 ~10:23    verbose                  LOST  (nothing on air by 10:53)
```

**The failure path is the 120-second `EV_TXCOMPLETE` timeout**, not the
`OP_TXRXPEND` early-skip. The elapsed time proves it: each lost frame costs
exactly 2 minutes, and the count of lost frames per stretch predicts the
observed gap to within seconds.

| stretch | cycles | frames attempted & lost | predicted | observed |
|---|---|---|---|---|
| 21:57 → 01:01 | 3 | 2 | 180 + 4 = 184 min | **184.1** |
| 01:01 → 05:15 | 4 | 7 | 240 + 14 = 254 min | **254.1** |
| 05:15 → 09:21 | 4 | 3 | 240 + 6 = 246 min | **246.2** |

The decisive detail is the 7.9 min difference between the two 4-cycle stretches.
Both slept the same 4 hours; the only difference is that 01:01→05:15 contains
the two cycles where the batch was full, so each attempted **three** frames
(data + diag + verbose) instead of one — 4 extra timeouts × 2 min = 8 min.
A clock-drift explanation cannot produce that difference; a per-frame timeout
predicts it exactly.

### Why this is worse than it looks

- **It is invisible in telemetry.** `transmitBatchAndWait()` sets
  `lastTxTimedOut = true`, but the resulting fault frame is *itself* dropped by
  the same failure, and the flag is cleared again by the next successful data
  uplink. So `tx_timeout` can never reach the air. Every frame we received
  reports `faults: []` / `healthy: true` while two thirds of transmissions were
  failing.
- **In PROD it would be far worse.** DEV re-tries every hour because it never
  sleeps; a PROD unit burns 2 min of *awake* radio time per lost frame on a
  battery budget that assumes a sub-second uplink.
- Data is not lost, only delayed — `uplinkScheduleOnTxSuccess()` is not called
  on timeout, so `ramCount` is preserved. That is why the 05:15 batch carried
  the six *newest* samples (from ~23:59 on) rather than the six from 21:57.

### What is not yet known

Why LMIC fails to start the TX within 120 s. It cannot be ordinary duty-cycle
back-off: an hour of radio silence clears any band or global duty debt, and the
intra-burst spacing we do see (~6 s) is exactly the 1% pacing, so the band plan
is behaving. Note that `LMIC_clrTxData()` returns immediately without clearing
`OP_TXRXPEND` when `OP_TXDATA` is already clear, so a timeout can leave the
stack wedged — worth checking as a cause of the *following* cycle's failure.

**Next step:** the unit is DEV-strapped and on USB. One failing cycle on the
serial console distinguishes the two paths — `"OP_TXRXPEND, skip send this
cycle"` / `"out-of-band frame: OP_TXRXPEND, defer to next cycle"` versus
`"FATAL: TX Timeout"` / `"out-of-band frame: TX timeout, clearing"` — and
logging `LMIC.opmode`, `LMIC.txend` and `LMIC.bands[].avail` alongside them
would settle it outright.

## 3. New defect: INA219 panel readings freeze after the first read of each boot

`panel_v` and `panel_ma` are **byte-identical in every verbose frame from 20:57
to 09:21** — 3.852 V / 15.9 mA, unchanged through the whole night *and* through
sunrise.

Cause, in `readAndBufferSensors()`:

```c
uint16_t busMv   = (uint16_t)(ina219.getBusVoltage_V() * 1000.0f);
float    currentMa = ina219.getCurrent_mA();
...
ina219.powerSave(true);   // ~15 uA between reads (S04-03)
```

`Adafruit_INA219::powerSave(true)` writes `MODE = POWERDOWN` into the config
register. **Nothing ever calls `powerSave(false)`.** From the second wake
onward the part is not converting, so the reads return the stale contents of the
conversion registers — the same numbers forever.

Consistent with this, each *boot* gets exactly one honest sample and then
freezes: 19:39 read 5.07 V / 0 mA (daylight), 20:53 read 3.848 V / 15.1 mA,
20:57 read 3.852 V / 15.9 mA — and 3.852 / 15.9 is what every later frame
repeats.

### Consequences

- **The harvest accumulator is fiction.** 15.9 mA × 3600 s = 16 mAh added every
  cycle, all night. It reported 192 mAh of harvest by 09:21 having harvested
  nothing.
- **The sun EWMA is rising in the dark.** `SUN_PRESENT_MV` is 3000 and the frozen
  bus voltage is 3852 mV, so `sun_present` is asserted on every cycle regardless
  of time of day. The EWMA went 0.004 → 0.396 overnight and is converging on 1.0.
- **A latent interval fault.** `SUN_BONUS_ENGAGE` is 0.55; at ~0.041/cycle the
  EWMA reaches it in roughly 7 more cycles (~16:30–17:00 CEST today) and, since
  `SUN_BONUS_RELEASE` is 0.45 and the EWMA only climbs, **the bonus latches on
  permanently and never releases**. The second gate saves us for now — it also
  needs `voltage_offset == 0`, i.e. ≥3.85 V, and the pack is at 3.72 V — but the
  moment the pack charges past 3.85 V the interval drops 2 steps for good, day
  and night.
- **The health check cannot see it.** `g_ina219ReadOk` only tests
  `busMv >= 500 && busMv < 20000`; a frozen 3852 passes, so
  `DIAG_FAULT_INA219_READ_FAIL` never fires. A stuck sensor is indistinguishable
  from a healthy one.

### Falsifiable prediction

The sun is up and the panel is charging. If this diagnosis is right, the next
frames will *still* report `panel_v: 3.852` / `panel_ma: 15.9`. If the panel
readings move today, the diagnosis is wrong.

## 4. Things that are fine

- **Battery.** 3.756 V at 20:57 → 3.720 V at 09:21: a smooth −36 mV over 12.4 h
  of night discharge. Sane, and the only genuinely measured power number we have.
- **Interval selection.** Summer (water 19.6–24.3 °C) → base index 4, plus
  `voltage_offset` 1 at 3.72 V → index 5 = 60 min. Correct per `policy_solar.h`.
- **Sensor.** `ds18b20_count` 1, no bus ambiguity, no read failures; the
  overnight cooling curve 24.3 → 19.6 °C and the 20.8 °C morning rebound are
  clean.
- **TTN formatter is current.** The repo decoder and TTN's `decoded_payload`
  agree on every frame from 20:53 onward, including `season: "Summer"`. The one
  disagreement is the 20:24 frame decoded as `"Winter"` — that predates the
  formatter re-upload of `8dc181f`, so it is history, not a live problem.
- **Gateway outage left almost no trace.** `f_cnt` is contiguous across 21:57, so
  nothing was lost. The only artefact is `rx_metadata[].time` running 33.5 min
  ahead of `received_at` before 21:57, snapping to −0.02 min at 21:57 and
  settling at +2.22 min from 01:01 — the gateway's clock resyncing across its
  restart. `received_at` is authoritative and was used throughout.

## 5. The empty MAC-only uplinks

19 of the 35 records carry no `f_port` and a zero-length payload: MAC-answer
uplinks LMIC sends in response to network downlinks, spaced ~6 s (the 1% band
pacing). They cluster after each app frame and taper off over the night — 5 at
19:40, 2 at 21:57, 1 at 01:01, none from 05:15 — consistent with ADR converging.
Not a fault, but they consume `f_cnt` and duty cycle, and they are the reason a
"5 uplinks in 26 seconds" burst appears at each boot.
