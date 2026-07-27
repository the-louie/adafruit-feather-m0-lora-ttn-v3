# adafruit-feather-m0-lora-ttn-2

Battery-powered LoRaWAN water-tank temperature sensor. An Adafruit Feather M0 LoRa (SAMD21 + RFM95) reads a DS18B20 via OneWire, batches six readings in RAM, uplinks a 9-byte payload to The Things Network over OTAA, and deep-sleeps between wakes. Region is **EU868** and the firmware refuses to compile without it (`#error` on `CFG_eu868`).

The firmware is `adafruit-feather-m0-lora-ttn-2.ino` plus a set of Arduino-free headers, each host-tested (`test/host/`, run with `test/host/run_tests.sh`):

| header | what |
|---|---|
| `season.h` | seasonal baseline + 1 °C hysteresis, shared by both variants |
| `power_policy.h` | the `PowerPolicy` interface + `voltageOffsetHyst()` |
| `policy_primary.h` | 6 V pack: bands, interval ladder |
| `policy_solar.h` | li-ion + panel: bands, two-gate bonus, 15-byte payload |
| `solar_signal.h` | sun EWMA, bonus gate, harvest accumulator |
| `payload.h` | payload bytes 0–8, encoders, sentinels |
| `uplink_schedule.h` | send decision + uplink counter |
| `persist.h` | `.noinit` state, magic+version+CRC |
| `variant_probe.h` | INA219 probe decision logic |
| `timekeeping.h` | GPS→UTC conversion |

The `.ino` holds the Arduino glue (I2C, RTC, LMIC, sensor reads); the headers hold the judgement. **Everything with judgement is host-tested; the glue is compile-verified.**

**Decoders live in `decoders/`, exported verbatim from TTN.** There is no application-level formatter — each device carries its own, and they differ. `ttn-decoder-v6.js` used to sit in this root and was **not** what production ran; it was deleted 2026-07-17 after causing a false diagnosis. Git has it if anyone ever needs it.

## Architecture

Two-phase flow, no application-level OS jobs:

1. **Commissioning** — while `LMIC.devaddr == 0`, `loop()` only spins `os_runloop_once()`. Never sleeps during join. In PROD, 3 minutes without a join triggers a 15-minute deep sleep and `NVIC_SystemReset()`.
2. **Operational** — wake → read sensor → buffer → uplink if due → sleep. Uplinks block on a `volatile bool txComplete` set from `EV_TXCOMPLETE`, with a 2-minute timeout that calls `LMIC_clrTxData()`.

Uplink fires when the buffer hits `batchTarget` (6) or on `wakeCounter == 1` — which the comments call the "fast-flush after join" but **is not**: `wakeCounter` is 4-bit, so it wraps to 1 every 16 wakes and re-fires with a partial batch (TODO #2). `ramCount` is cleared **only** after a confirmed `EV_TXCOMPLETE`; a timeout keeps the batch for the next cycle.

## Design rules

These are deliberate and load-bearing. Don't undo them without asking:

- **No FlashStorage, ever.** SAMD21 RAM survives `LowPower.deepSleep()`. Flash writes cost power, wear the part, and disturb the SPI bus the radio shares. Session state and the temperature array live in volatile RAM.
- **No application OS jobs.** `os_setTimedCallback` and friends clog LMIC's MAC-timing queue. Application flow stays linear.
- **No `delay()` while the radio is active.** It desynchronizes the MAC layer. Waits are `millis()`-based loops that call `os_runloop_once()` — that's why the post-join blink and the DEV sleep are written the way they are.
- **Respect the duty cycle.** Never touch `LMIC.globalDutyAvail` or `LMIC.bands[].avail`. A previous "frozen time" bypass was removed deliberately; the first TX after a long sleep may legitimately be delayed until LMIC permits it.
- **Pin 11 straps the mode, not pin 13.** Pin 13 is wired to the onboard LED, so `INPUT_PULLUP` on it bleeds current through the LED during deep sleep. Pin 13 is output-only, for visual debug.
- **`delay(5000)` + `SPI.begin()` at the very top of `setup()`.** The RFM95 needs to stabilize before any SPI traffic, or it comes up blind and deaf.
- **`LMIC_setClockError((uint32_t)MAX_CLOCK_ERROR * 5 / 100)`** — the cast matters; without it the expression overflows 16 bits. The M0's RC oscillator drifts and needs the 5% relaxation.
- **`LMIC_setLinkCheckMode(0)` inside `EV_JOINED`.** Otherwise the gateway burns its duty cycle on MAC ACKs.
- **`currentIntervalIndex` has exactly two write points:** `setup()` and the post-`EV_TXCOMPLETE` block. Interval changes only after a successful uplink.

## Two power variants, chosen at boot

A runtime I2C probe for the INA219 selects the variant — one binary for every board.

**The probe MUST soft-reset the INA219 before reading its config (`variant_probe.h` / `probeIna219Once`).** The probe recognises an INA219 by its config register reading the power-on reset value `0x399F`. But after `setCalibration_16V_400mA()` the register reads **`0x019F`**, and on a **warm MCU reset** (RST button, watchdog, the PROD join-failure `NVIC_SystemReset()`) the INA219 stays powered and still holds `0x019F` — so the probe read `0x019F ≠ 0x399F`, misdetected a present sensor as absent, and booted a **solar unit into the PRIMARY policy** (the A1 catastrophe: wrong bands → parks at a long interval). Writing RST (config bit 15) before the read returns it to `0x399F`. This was a real defect confirmed on gisebo-05 2026-07-27 (`docs/dev-notes/20260727-1848_ina219-warm-reset-misdetect.md`) — do not remove the soft-reset.

| | primary | solar |
|---|---|---|
| pack | 6 V (4×AA or 2× 3 V lithium) | 1S2P 18650 li-ion + panel |
| bands | 5.0 / 4.3 / 3.5 V | 3.85 / 3.65 / 3.45 V |
| payload | 9 bytes | 15 bytes (6 appended) |
| FPorts | 10 (PROD) / 20 (DEV) | 11 / 21 |

Both share the season machine and the voltage-band hysteresis. The solar variant adds a sun-presence EWMA (keyed on panel **bus voltage**, not current — current collapses to 0 when the pack is full), a fixed 2-step interval bonus gated on `voltage_offset == 0` AND the sun EWMA, and a harvest accumulator. See `docs/solar-variant-design.md`.

## The fleet

| device | protocol | status |
|---|---|---|
| gisebo-01 | 9-byte v6, 30 min | **PRODUCTION, frozen; retired at cutover** |
| gisebo-04 | 8-byte v5, 5 min fixed | test unit in a fridge (cold lithium test) — **do not disturb** |
| gisebo-05 | v7 solar | **LIVE on the bench (DEV-strapped, FPort 21): flashed 2026-07-27, joined, solar detected, reporting** |

gisebo-05 **replaces** gisebo-01 (sprint 08 cutover); the two are never in production together. Decoders are per-device (`decoders/`), one per unit. gisebo-05 was registered in TTN (`telamon-temperature`, EU868) and flashed with the current firmware 2026-07-27; its OTAA keys are derived on boot from the silicon serial, so its TTN DevEUI is the derived one (`86A2A75D253A16AC`), not the app-block DevEUI in the older provisioning note.

## DEV vs PROD

`STRAP_PIN` (11) read once in `setup()`: LOW (tied to GND) = DEV (`runMode 1`), floating = PROD (`runMode 0`). Only four things differ:

| | PROD | DEV |
|---|---|---|
| FPort | 10 | 20 |
| USB/Serial | `USBDevice.detach()`, no Serial | Serial at 9600, `logPrint*` active |
| Sleep | `LowPower.deepSleep()` | busy-wait running `os_runloop_once()` |
| Sensor-conversion wait | `LowPower.idle(750)` | 750 ms `os_runloop_once()` loop |

DEV can't deep-sleep or idle without dropping USB — the native USB peripheral depends on the SAMD21 clocks. `logPrint`/`logPrintln` are no-ops in PROD, so logging is free to add.

## Uplink protocol (9 bytes, "v6")

| Byte | Meaning |
|---|---|
| 0 | Interval index 0–10 |
| 1–2 | 12-bit battery offset from 3000 mV, then 4-bit sequence in the low nibble of byte 2 |
| 3–8 | Six temperatures, newest first |

Temperature byte: `0` = -10 °C, `200` = +30 °C at 0.2 °C/step; `250` = no value, `251` = too cold, `252` = too warm. Battery: `0` = 3.000 V, `4095` = 7.095 V, clamped both ends. Mode comes from the FPort, never from a payload byte.

Sequence is `wakeCounter & 0x0F`. The decoder reads `sequence == 0` as a reboot — **it has never once fired**, because the value only ever takes {1, 7, 13}. A free-running 4-bit counter cannot distinguish a reboot from a wraparound in principle; the real reboot signal is `f_cnt` resetting in TTN metadata, which costs no payload at all (TODO #2).

Interval index → minutes: `[unused, 1, 5, 15, 30, 60, 120, 360, 720, 1440, 10080]`. Byte 0 is sent every time so the backend can extrapolate timestamps across an interval change mid-history.

Any change to the payload must land in `transmitBatchAndWait()` and the device's decoder in `decoders/` together.

## Diagnostics uplinks (added 2026-07-27, `diagnostics.h`)

Health/telemetry rides **separate FPorts** from the data payload, decoded by the same per-device formatter (`decoders/gisebo-05-v7.js`). All are **out-of-band** — sent after the data uplink via `txFrameAndWait()`, and never touch `currentIntervalIndex` or the uplink counter. The full FPort map:

| purpose | PROD | DEV | length | notes |
|---|---|---|---|---|
| data (primary) | 10 | 20 | 9 B | |
| data (solar) | 11 | 21 | 15 B | |
| **fault/health** | **1** | **2** | 11 B | variant-independent |
| **verbose full-state** | — | **3** | 22 B | **DEV-only** |

- **Fault frame (FPort 1/2)** — the `diagnostics.h` fault bitmap + reset cause + INA219 probe config + battery, so a field unit with no USB reports a dead DS18B20, flaky I2C, decayed-RAM persist, TX timeout, or low battery. Send policy: **one per boot** (after the first read), plus a **new distinct fault reported promptly**, plus a persistent fault re-alerted at most once/day — spam-proof with or without a clock. The rate-limit latch lives in the CRC-protected persist struct. It exists because the core 9-byte payload carries **no status at all** (only the solar variant's byte 14 does). See `docs/dev-notes/20260726-1905_diagnostic-error-uplink.md`.
- **Verbose frame (FPort 3, DEV-only)** — a 22-byte full-state snapshot (battery, panel V/I, sun EWMA, harvest, season/band, interval, sensor, INA219 config, reset/boot, faults), sent once at boot then **~hourly** via a plain `millis()` gate (DEV never deep-sleeps; `VERBOSE_INTERVAL_MS` is the knob — battery cost is a non-issue in DEV). Confirms "everything looks OK", not just "no faults". PROD emits none. See `docs/dev-notes/20260727-1804_verbose-dev-diagnostics-frame.md`.
- **Season is on the wire for the first time** (verbose byte 5). `SeasonState` is `WINTER=0, MID=1, SUMMER=2` (`season.h`) — the decoder's `SEASON_NAMES` must match that order.
- Any new diagnostic FPort must land in the `.ino`, `diagnostics.h`, and the decoder together, and be re-uploaded to the TTN formatter.

## Interval selection

`calculate_interval_index(tempC, voltageV)` picks a seasonal base from water temperature with 1 °C hysteresis (Summer ≥16 °C → index 4; Fall/Spring 8–15 °C → 5; Winter <8 °C → 7), then adds a battery penalty of 0–3 steps (≥5.0 V, ≥4.3 V, ≥3.5 V, below). The result is clamped to 1–10.

Season is driven by temperature rather than voltage on purpose: a battery that reads low because it's cold shouldn't permanently lock the device into a long interval. When the water warms, the season transitions and the base interval shortens again. The hysteresis exists to stop flapping at the 16 °C and 8 °C boundaries. Invalid/NaN temperature leaves the season state untouched.

**The pack is 6 V nominal** — 4×AA, or 2× 3 V lithium. Not a LiPo. This is why the voltage bands are 5.0 / 4.3 / 3.5 V and why the payload encodes 3.0–7.095 V; read against a LiPo's 4.2 V ceiling the thresholds look unreachable and the whole penalty ladder looks broken. `getBatteryVoltage()` reads A7 through a 100k/100k divider, so the ADC saturates around 6.59 V and the payload's top clamp (7.095 V) is unreachable in practice.

Two behaviors worth knowing when reasoning about the algorithm:

- **Chemistry changes how much the battery ladder engages — and production telemetry now distinguishes the two.** Alkaline AAs slope steadily through the bands; 2× 3 V lithium sits flat for most of its life then falls off a cliff. Measured 2026-07-17: `gisebo-01` reads **+12.8 mV/°C (r = 0.93)** — alkaline's temperature coefficient — while `gisebo-04`, on lithium in a fridge, shows no correlation and drifts just **−2.5 mV/day** at a steady 9 °C. The telemetry identifies the chemistry without anyone opening the box.
- **The cold-battery illusion is real and measured.** That +12.8 mV/°C extrapolates to ~321 mV of temperature-driven drift across a 25 °C season — a pack at constant charge reading a third of a volt lower in winter. This is exactly why season is driven by water temperature and never by voltage. See `docs/dev-notes/20260717-1100_battery-voltage-tracks-temperature-in-production.md`.
- **Season steps one level per uplink.** The transitions are an `else if` chain, so a jump from Summer to <8 °C water reaches Fall/Spring on one uplink and Winter only on the next. `setup()` starts at `current_season_state = 2` (Summer), so a cold-start in winter takes two uplink cycles to settle — expect a rebooted device to transmit sooner than steady state for a short while.

## Build & test

Run **`./scripts/setup-toolchain.sh`** as your normal user (no root). It installs arduino-cli, the Adafruit SAMD core and the libraries, configures the region, compiles, and prints the baseline. Idempotent.

```
arduino-cli compile --fqbn adafruit:samd:adafruit_feather_m0 .
# baseline 2026-07-17: 61632 bytes (23%) of program storage
# 2026-07-27 with diagnostics + verbose + probe fix: ~72.9 kB (27%)
```

Pinned versions: adafruit:samd 1.7.17, **MCCI LMIC 6.0.1**, Arduino Low Power 1.2.2, RTCZero 1.6.0, OneWire 2.3.8, DallasTemperature 4.0.6.

**Flashing does NOT work from this Claude environment — compile only.** `arduino-cli upload` / direct `bossac` cannot reach the SAMD21 bootloader over this USB passthrough (the 1200 bps touch resets the board but the bootloader port never re-surfaces, and `bossac` hangs — no CDC bulk data). No `sudo`/root to reset the USB stack. **The operator flashes on their own machine:** compile here, hand off the `.ino.bin` (from `~/.cache/arduino/sketches/<hash>/`), and flash locally — Arduino IDE, or double-tap RESET + `bossac … --offset=0x2000 -w -v <bin> -R`. Windows PowerShell needs the call operator: `& "…\bossac.exe" …`. Not UF2 — the plain Feather M0 uses the serial BOSSA bootloader. (Also in memory `flashing-feather-m0-in-this-env`.)

**The region trap.** `CFG_eu868` must be set in the library's `lmic_project_config.h` — inside the library, not this repo, so it is invisible and unversioned. `reference/lmic_project_config.h` is the known-good copy. The stock file ships with **`CFG_us915` enabled**, so naively adding `eu868` defines two regions and LMIC refuses to build. Disable every region, enable exactly one. `CFG_sx1276_radio` is not a region — it picks the RFM95's chip and stays.

The sketch's `#ifndef CFG_eu868 / #error` guard is verified working: both a missing region and the stock us915 default are refused at compile time, so a US915 binary cannot reach a device by accident.

**DIO1 must be physically jumpered to pin 6**; the pin map assumes it.

## Tests

```
npm test          # or: node test/run.js
```

No framework, no dependencies — a script with an exit code. `test/harness.js` loads a decoder **as text** and evals it, so the file under test is byte-for-byte the file pasted into TTN; adding `module.exports` for `require()` would let them drift, which is exactly how `ttn-decoder-v6.js` diverged from what actually ran.

Live vectors in `test/fixtures-live.json` replay real production uplinks, and **the expected values are TTN's own `decoded_payload`** — production truth, not our assumptions. If our understanding of the protocol is wrong, the tests fail.

The harness self-tests too: a suite that silently does nothing is worse than none.

**There is no on-device test hardware** — units are ordered but arrive later than sprint 03. Everything currently ships verified by compilation alone; sprints 06–07 verify retroactively. Building the executable checks is sprint 01–02 work (a Node decoder harness and host-side firmware tests).

Note a DEV board cannot verify everything: the `idle(750)` defect is **PROD-only by construction**, because the DEV path uses an `os_runloop_once()` loop instead. That asymmetry is why it survived for months.

## Conventions

- **Commit in small batches — far more often than once per task, let alone once per sprint.** The unit of a commit is *one function, plus its test if one is available, plus its documentation*. Those three land together: a function without its test or its doc is not a finished commit, and a batch of unrelated functions is not one commit. A 1–2 hour task will usually produce several commits, not one.
- `docs/dev-notes/` holds one dated note per change (`YYYYMMDD-HHMM_slug.md`) covering summary, rationale, and verification. Add one for any non-trivial firmware change.
- `TODO.md` holds detailed items (problem, solution, verification); `TODO-summarized.md` mirrors each as one `title - complexity - estimated time - summary` line plus a summary at the bottom. The two are edited together. Drop items once fully implemented, and keep partial work at the top regardless of priority.
- `.cursor/skills/master-plan/` and `.cursor/skills/domain-knowledge/` are the authoritative statements of the design rules and the LoRaWAN/LMIC background. They are **gitignored** (`.cursor` in `.gitignore`), so they exist only locally — read them before substantial work, and keep them in sync when a design rule changes.
- This directory is its own git repo, separate from the sibling `waveshare-rp2040-lora-ttn` project. Commit in the repo you changed.

## Where the work is tracked

- **`TODO.md`** — 14 work items, including two **confirmed production defects** (see below). `TODO-summarized.md` mirrors it one line per item.
- **`docs/sprints/`** — 7 sprints, ~116 tasks of 1–2 h. `docs/sprints/README.md` is the index and states the plan's own risks.
- **`docs/solar-variant-design.md`** — the agreed design for the solar/li-ion variant, including the protocol versioning scheme (v6 current, v7 = new work) and the rejected alternatives.
- **`docs/multi-sensor-v4-analysis.md`** — analysis only, nothing scheduled: what v3 must do so adding box/air/depth sensors in v4 stays cheap.
- **`docs/dev-notes/real-world-data__20260716.json`** — 107 production uplinks. The evidence for both confirmed defects.

## Confirmed defects — do not trust current PROD data

Both proven from the 2026-07-16 capture, not from reasoning:

- **`LowPower.idle(750)` does not wait.** `ArduinoLowPower::setAlarmIn()` does `rtc.setAlarmEpoch(now + millis/1000)` — integer division, so 750 ms becomes a zero-second alarm and it returns early. The DS18B20 read then returns the *previous* conversion, so **every PROD temperature reading is lagged one wake interval**. PROD-only: the DEV path spends the window in an `os_runloop_once()` loop, which is exactly why bench testing never found it.
- **The fast-flush fires every 16 wakes**, not once per join — `wakeCounter` is 4-bit and wraps, re-triggering `wakeCounter == 1` with a partial batch. Sequence takes only {1, 7, 13} across 107 uplinks, so **`rebootDetected` has never once fired**.

The `{1, 7, 13}` signature identifies un-reflashed units.

## Known gaps

- **OTAA credentials are derived on boot from the SAMD21 silicon serial** (`keygen.h` + gitignored `keygen_salt.h`), so one binary serves every board and there are no hardcoded per-board keys to mis-flash. The secret salt is unversioned (like `lmic_project_config.h`); the build won't compile without `keygen_salt.h` (copy from `keygen_salt.h.example`). See `docs/generate-keys-from-feather-serial.md`.
- **`doc/test-payloads.md` is stale** — its vectors are 8-byte V5 payloads and predate the interval byte. Don't trust them against the current decoder.
