# Waveshare RP2040-LoRa: feasibility, effort, and robustness risk

Date: 2026-07-30. Question from the operator: can the Waveshare RP2040-LoRa and
the Adafruit Feather M0 LoRa live in one codebase, what would the port cost, and
what does it do to robustness?

Short answers: **one codebase, one binary — no. Two repos sharing the
host-tested headers — yes, and that is the right shape. Effort: 75–125 h
(2–3 working weeks). Robustness risk: high, and concentrated in exactly the
layer that produced every defect this project has found.** There is also a
prior question about whether this board suits a solar node at all, in §6.

## 1. The decisive fact: it is an SX1262, not an SX1276

The Feather M0 LoRa carries an **RFM95 (SX1276)**. The RP2040-LoRa carries an
**SX1262**. These are not variants of one part:

| | SX1276 (current) | SX1262 (RP2040-LoRa) |
|---|---|---|
| SPI model | register read/write | **command/opcode based** |
| Handshake | none | **BUSY pin, up to 3.5 ms** |
| Antenna switch | none on our board | **PE4259 needing complementary control** |
| Reference osc | crystal | **TCXO powered from DIO3** |

**MCCI LMIC does not support the SX1262**, confirmed across the TTN forum, the
RadioLib project and the deprecated matthijskooijman LMIC README. This is not a
pin-map change; the entire radio driver differs. So the RP2040 board needs a
different LoRaWAN stack, which means **every LMIC-specific design rule in
CLAUDE.md stops applying to it**.

Realistic stacks:

- **RadioLib** — drives SX126x *and* SX127x, LoRaWAN Class A/C. The only option
  that could in principle unify both boards.
- **LMICPP-Arduino** — LMIC-in-C++ with SX1262 support, but SX1262 support is
  documented as TCXO-only (fine here, the board has one).

## 2. Hardware facts worth having before any work starts

Pin map, from a working Arduino sketch (nwgat) corroborated by the MeshCore
board definition:

```
SX1262:  NSS GP9   RESET GP23   BUSY GP18   DIO1 GP16
SPI1:    SCK GP10  MOSI GP11    MISO GP8      (SPI1 is mandatory)
RF sw:   DIO2 -> PE4259 CTRL,  GP17 -> !CTRL (RXEN)
```

Three traps already documented by others, all of which would have cost days to
find independently:

1. **TCXO voltage is finicky.** RadioLib defaults to assuming a TCXO; wrong
   voltage gives `-706`/`-707`, absent-TCXO settings give chip-not-found. The
   MeshCore project shipped a fix moving `SX126X_DIO3_TCXO_VOLTAGE` to 1.8 V,
   then a later report says 1.6 V was what actually started the radio. Expect to
   bisect this on hardware.
2. **The antenna switch needs both signals.** DIO2 drives CTRL via
   `SX126X_DIO2_AS_RF_SWITCH`, and **GP17 must drive the inverted !CTRL**.
   MeshCore PR #2159 records that leaving GP17 floating puts the PE4259 in an
   undefined state — a board that appears to work at short range and fails
   subtly at distance. That is a textbook robustness landmine.
3. **SPI1 must be initialised by the library**, not hand-configured. The same PR
   traced `RADIOLIB_ERR_CHIP_NOT_FOUND` to calling `SPI1.begin(false)` and then
   passing NULL, which skipped RadioLib's own SPI setup.

**Not documented anywhere I could find, and load-bearing for us: a battery
connector, a charger, or a battery-sense divider.** The Feather M0 has a JST
battery input, a LiPo charger, and a 100k/100k divider on A7 that
`getBatteryVoltage()` depends on. The RP2040-LoRa appears to have none of these.
Every power feature in this firmware — the voltage bands, the interval penalty
ladder, the low-battery fault, and the newly-adopted battery-trend metric —
depends on reading pack voltage. On this board that is **added external
hardware**, not a software port.

## 3. The power problem, which is the real blocker

The RP2040 is a poor fit for a sleeping sensor, and the numbers are not close:

| | SAMD21 (current) | RP2040 |
|---|---|---|
| Deep sleep w/ timed wake | `LowPower.deepSleep()`, RTC-alarm, spec <100 µA | **SLEEP ~390 µA** |
| Lowest mode | — | DORMANT ~180 µA, **but the RTC does not run**, so no timed wake without an external clock |
| Arduino support | first-class (`ArduinoLowPower`) | **not exposed by arduino-pico**; needs rebuilding `libpico.a` to include `pico_sleep`, or a third-party wrapper |

This firmware's whole cadence is `deepSleep(interval)` → RTC alarm → wake. On the
RP2040 the mode that keeps the RTC costs ~390 µA, and the cheaper mode cannot
wake itself on time at all.

Put against this project's own measured budget: the Feather M0 board draws
**~290 µA total** (≈6.9 mAh/day — regulator, charger and USB leak dominated, per
`policy_primary.h`). The RP2040 in RTC-capable sleep is ~390 µA **for the MCU
alone**, before the Waveshare board's regulator and LEDs. A realistic
500–700 µA gives **12–17 mAh/day, roughly double** the current platform.

Whether that matters depends entirely on season. Yesterday's measured July
harvest was ~519 mAh/day, so summer is untroubled. But the entire winter
interval ladder (index 7 = 6 h) exists because winter harvest at 57.8°N
collapses, and the documented energy-balance figure is **7–28 mAh/day**. Doubling
quiescent draw halves the winter margin on a device whose winter margin is the
design's tightest constraint.

## 4. What ports for free, and what does not

The architecture's central split pays off here. Everything with judgement is
Arduino-free and host-tested, and **all of it ports unchanged**:

`season.h`, `power_policy.h`, `policy_primary.h`, `policy_solar.h`,
`solar_signal.h`, `payload.h`, `uplink_schedule.h`, `persist.h`,
`variant_probe.h`, `timekeeping.h`, `diagnostics.h`, `ina219_bus.h` — roughly
1400 lines, plus the entire host test suite (332 assertions) and the decoder.

What does **not** port, essentially at all:

| current mechanism | RP2040 status |
|---|---|
| MCCI LMIC + `os_runloop_once()` linear flow | replaced wholesale (RadioLib is blocking `sendReceive()`) |
| `txComplete` on `EV_TXCOMPLETE`, 2-min timeout | different API and semantics |
| the `os_getTime()` sleep-loop fix (clock extender) | LMIC HAL-specific; does not exist |
| `LMIC_setTxData2()` return check, `waitForTxReady()`, `OP_POLL`/`OP_TXRXPEND` | LMIC opmode concepts; gone |
| duty-cycle handling via `LMIC.bands[].avail` | RadioLib's own model |
| LMIC in-RAM session across sleep | RadioLib needs explicit `setBufferSession()`/`setBufferNonces()`; **devNonce must be monotonic and persistent or the network rejects joins** |
| `.noinit` persist struct | RAM survives soft reset, but needs a linker section for arduino-pico *and* now must also carry RadioLib's session+nonce buffers |
| `keygen.h` reading SAMD21 silicon serial at `0x0080A00C…` | RP2040 has **no silicon serial**; must use the flash chip's 64-bit unique ID — different length, different source, and tied to the flash part rather than the MCU |
| `getBatteryVoltage()` on A7 divider | no divider on the board (§2) |
| `#error CFG_eu868` region guard | RadioLib selects region differently |

Note the keygen consequence: a 64-bit input where the derivation expects 128
bits means the DevEUI/AppKey derivation must change for that platform, so the two
platforms produce keys from different-shaped inputs. That needs a deliberate
domain separator, or every RP2040 unit gets re-registered in TTN.

## 5. Effort, honestly

| work | hours |
|---|---|
| RadioLib LoRaWAN glue: OTAA join, uplink, RX windows, region | 20–30 |
| Sleep + power path (the risky one; `pico_sleep` integration, wake source) | 15–25 |
| Keygen on the flash unique ID + persist/session/nonce buffers | 6–10 |
| Battery sensing: external divider design + ADC path | 5–8 |
| Build system: second FQBN, per-platform sources, hash stamping | 6–10 |
| New per-device decoder + test vectors | 8–12 |
| Hardware bring-up and live debugging | 15–30 |
| **total** | **75–125 h (2–3 working weeks)** |

The bring-up line is not padding. This week alone, two days went to defects that
*only live multi-hour data* revealed on a platform the project already knows
well, with a mature stack. A new MCU, a new radio family and a new LoRaWAN stack
is three unfamiliar variables at once.

## 6. Robustness risk: the part I would push back on

Every defect this project has found lived in the **glue**, was **invisible to
inspection**, and surfaced only from **days of live data**:

- `idle(750)` truncation — PROD-only by construction, undetected for four months
- LMIC clock-extender starvation — DEV-only by construction, found by gap arithmetic
- `powerSave` freeze — looked perfectly healthy for a full night
- `SUN_PRESENT_MV` unreachable at night — structurally invisible until the first honest night

The pattern is explicit in CLAUDE.md: *"a DEV board cannot verify everything…
that asymmetry is why it survived for months."* A second platform makes that
matrix **{SAMD21, RP2040} × {DEV, PROD}** — four glue paths, no two of which
cross-validate, and each observed for half as long as today. With two *different
radio stacks*, a defect found on one board says nothing about the other.

**This is why `#ifdef ARDUINO_ARCH_RP2040` inside the current `.ino` would be the
worst available choice.** It would put two un-cross-testable code paths in one
file, which is precisely the shape of the `idle(750)` and clock-extender bugs,
and it would put the RP2040's immature radio path in the same file as the
gisebo-05 firmware that is finally defect-free and verified.

## 7. Recommendation

**Two repos, sharing the headers — not one codebase.**

1. Extract the Arduino-free headers into a shared location (git submodule, or a
   `core/` directory vendored into both repos with a sync check in `build.sh`).
   They are already the portable, host-tested, valuable part, and they carry the
   payload contract that must not diverge.
2. `waveshare-rp2040-lora-ttn` gets its own `.ino`, its own RadioLib glue, its
   own decoder, and its own `build.sh`. CLAUDE.md already anticipates this
   sibling repo.
3. **Do not** attempt a unified `.ino`. There is no worthwhile common
   abstraction over LMIC and RadioLib, and the attempt would double the
   un-cross-testable surface.

**Sequencing:** do it *after* the current backlog closes (items 15 and 17
verification, item 10 backend). gisebo-05 has been defect-free for less than a
day; spending that hard-won stability on a second platform now is poor timing.

**And the prior question worth asking:** is the RP2040 the right board for this
application? For a solar node at 57.8°N it brings roughly double the quiescent
draw, no RTC-capable deep sleep, no battery charger, and no battery sense — while
its strengths (dual core, USB host, plentiful GPIO, price) serve nothing this
firmware needs. If the driver is supply chain or boards-on-hand, that is a fair
reason, but it should be weighed against a halved winter margin. If the driver is
wanting the SX1262 specifically — better sensitivity, lower TX current — then a
low-power MCU paired with an SX1262 (RAK3172-class STM32WL, or an SX1262 module
on a SAMD21/STM32L0) keeps this architecture intact and costs a fraction of the
work.

## Sources

- [Waveshare RP2040-LoRa wiki](https://www.waveshare.com/wiki/RP2040-LoRa) · [product page](https://www.waveshare.com/rp2040-lora.htm) — SX1262, SPI register model, BUSY semantics, DIO2 as RF switch
- [nwgat.ninja RP2040-LoRa setup](https://nwgat.ninja/rp2040-lora/) — working pin map, SPI1 requirement, arduino-pico + RadioLib
- [MeshCore PR #2159](https://github.com/meshcore-dev/MeshCore/pull/2159) — chip-not-found root cause, GP17/!CTRL antenna switch fix
- [MeshCore PR #1425](https://github.com/meshcore-dev/MeshCore/pull/1425) — TCXO voltage change
- [RadioLib troubleshooting: TCXO vs XTAL](https://github.com/jgromes/RadioLib/wiki/Troubleshooting-Guide) — the -706/-707 failure mode
- [RadioLib](https://github.com/jgromes/RadioLib) — SX126x + SX127x, LoRaWAN Class A/C
- [LMICPP-Arduino (SX1262, TCXO-only)](https://github.com/ngraziano/LMICPP-Arduino) · [TTN thread](https://www.thethingsnetwork.org/forum/t/lmicpp-arduino-lmic-library-with-sx1262-support/41018) — and the confirmation MCCI LMIC does not work with SX1262
- [RadioLib LoRaWAN persistence](https://github.com/radiolib-org/radiolib-persistence) · [devNonce reuse failure](https://github.com/jgromes/RadioLib/issues/1480) — session/nonce persistence requirements
- [RP2040 sleep-mode currents](https://forums.raspberrypi.com/viewtopic.php?t=316458) · [RPi power-switching app note](https://pip.raspberrypi.com/categories/685-whitepapers-app-notes/documents/RP-004339-WP/Power-switching-RP2040-for-low-standby-current-applications.pdf) · [arduino-pico sleep discussion](https://github.com/earlephilhower/arduino-pico/discussions/1544) · [arduino-pico-sleep example](https://github.com/matthias-bs/arduino-pico-sleep)
- [pico_unique_id (flash 64-bit ID)](https://cec-code-lab.aps.edu/robotics2/resources/pico-c-api/group__pico__unique__id.html) · [SAMD21 128-bit serial](https://support.microchip.com/s/article/Reading-unique-serial-number-on-SAM-D20---SAM-D21---SAM-R21-devices)
- [Arduino forum: RadioLib with RP2040-LoRa](https://forum.arduino.cc/t/radiolib-with-rp2040-lora-waveshare-module/1275325) · [fritzenlab writeup](https://fritzenlab.net/2024/07/29/waveshare-rp2040-lora-hf-with-arduino/) (notable: author could not get LoRa working)
