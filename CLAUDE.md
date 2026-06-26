# adafruit-feather-m0-lora-ttn-2

Battery-powered LoRaWAN water-tank temperature sensor. An Adafruit Feather M0 LoRa (SAMD21 + RFM95) reads a DS18B20 via OneWire, batches six readings in RAM, uplinks a 9-byte payload to The Things Network over OTAA, and deep-sleeps between wakes. Region is **EU868** and the firmware refuses to compile without it (`#error` on `CFG_eu868`).

The whole firmware is one sketch: `adafruit-feather-m0-lora-ttn-2.ino`. `ttn-decoder-v6.js` is the matching payload formatter, pasted into the TTN console by hand.

## Architecture

Two-phase flow, no application-level OS jobs:

1. **Commissioning** — while `LMIC.devaddr == 0`, `loop()` only spins `os_runloop_once()`. Never sleeps during join. In PROD, 3 minutes without a join triggers a 15-minute deep sleep and `NVIC_SystemReset()`.
2. **Operational** — wake → read sensor → buffer → uplink if due → sleep. Uplinks block on a `volatile bool txComplete` set from `EV_TXCOMPLETE`, with a 2-minute timeout that calls `LMIC_clrTxData()`.

Uplink fires when the buffer hits `batchTarget` (6) or on `wakeCounter == 1` (the first reading after join — "fast-flush"). `ramCount` is cleared **only** after a confirmed `EV_TXCOMPLETE`; a timeout keeps the batch for the next cycle.

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

Temperature byte: `0` = -10 °C, `200` = +30 °C at 0.2 °C/step; `250` = no value, `251` = too cold, `252` = too warm. Battery: `0` = 3.000 V, `4095` = 7.095 V, clamped both ends. Sequence is `wakeCounter & 0x0F`; the decoder reads `sequence == 0` as a reboot. Mode comes from the FPort, never from a payload byte.

Interval index → minutes: `[unused, 1, 5, 15, 30, 60, 120, 360, 720, 1440, 10080]`. Byte 0 is sent every time so the backend can extrapolate timestamps across an interval change mid-history.

Any change to the payload must land in `transmitBatchAndWait()` and `ttn-decoder-v6.js` together.

## Interval selection

`calculate_interval_index(tempC, voltageV)` picks a seasonal base from water temperature with 1 °C hysteresis (Summer ≥16 °C → index 4; Fall/Spring 8–15 °C → 5; Winter <8 °C → 7), then adds a battery penalty of 0–3 steps (≥5.0 V, ≥4.3 V, ≥3.5 V, below). The result is clamped to 1–10.

Season is driven by temperature rather than voltage on purpose: a battery that reads low because it's cold shouldn't permanently lock the device into a long interval. When the water warms, the season transitions and the base interval shortens again. The hysteresis exists to stop flapping at the 16 °C and 8 °C boundaries. Invalid/NaN temperature leaves the season state untouched.

**The pack is 6 V nominal** — 4×AA, or 2× 3 V lithium. Not a LiPo. This is why the voltage bands are 5.0 / 4.3 / 3.5 V and why the payload encodes 3.0–7.095 V; read against a LiPo's 4.2 V ceiling the thresholds look unreachable and the whole penalty ladder looks broken. `getBatteryVoltage()` reads A7 through a 100k/100k divider, so the ADC saturates around 6.59 V and the payload's top clamp (7.095 V) is unreachable in practice.

Two behaviors worth knowing when reasoning about the algorithm:

- **Chemistry changes how much the battery ladder engages.** Alkaline AAs slope steadily from ~6.4 V down through all three bands, so the interval stretches gradually as intended. 2× 3 V lithium has a flat discharge curve — it sits above 5.0 V (offset 0) for most of its life, then drops through 4.3 and 3.5 quickly at the end. On lithium you get the seasonal baseline almost throughout, then a late cliff, not a gentle taper.
- **Season steps one level per uplink.** The transitions are an `else if` chain, so a jump from Summer to <8 °C water reaches Fall/Spring on one uplink and Winter only on the next. `setup()` starts at `current_season_state = 2` (Summer), so a cold-start in winter takes two uplink cycles to settle — expect a rebooted device to transmit sooner than steady state for a short while.

## Build & test

Arduino IDE / arduino-cli, board **Adafruit Feather M0**, with MCCI LoRaWAN LMIC, ArduinoLowPower, OneWire, and DallasTemperature. `CFG_eu868` must be set in the library's `lmic_project_config.h` — it is not settable from the sketch. **DIO1 must be physically jumpered to pin 6**; the pin map assumes it.

There is no automated test suite. Verification is flashing a strapped DEV board and watching the serial log, plus running payload hex through the decoder in the TTN console.

## Conventions

- **Commit in small batches — far more often than once per task, let alone once per sprint.** The unit of a commit is *one function, plus its test if one is available, plus its documentation*. Those three land together: a function without its test or its doc is not a finished commit, and a batch of unrelated functions is not one commit. A 1–2 hour task will usually produce several commits, not one.
- `docs/dev-notes/` holds one dated note per change (`YYYYMMDD-HHMM_slug.md`) covering summary, rationale, and verification. Add one for any non-trivial firmware change.
- `TODO.md` holds detailed items (problem, solution, verification); `TODO-summarized.md` mirrors each as one `title - complexity - estimated time - summary` line plus a summary at the bottom. The two are edited together. Drop items once fully implemented, and keep partial work at the top regardless of priority.
- `.cursor/skills/master-plan/` and `.cursor/skills/domain-knowledge/` are the authoritative statements of the design rules and the LoRaWAN/LMIC background. They are **gitignored** (`.cursor` in `.gitignore`), so they exist only locally — read them before substantial work, and keep them in sync when a design rule changes.
- This directory is its own git repo, separate from the sibling `waveshare-rp2040-lora-ttn` project. Commit in the repo you changed.

## Known gaps

- **OTAA credentials are hardcoded** in the sketch, with a second board's DevEUI/AppKey commented out beside them — flashing the wrong keys is a live hazard. `docs/generate-keys-from-feather-serial.md` proposes deriving them from the SAMD21 silicon ID instead, so one binary serves every board. Not implemented.
- **`doc/test-payloads.md` is stale** — its vectors are 8-byte V5 payloads and predate the interval byte. Don't trust them against the current decoder.
