---
name: domain-knowledge
description: Provides LoRaWAN, TTN, MCCI LMIC, and Feather M0/RFM95 domain knowledge. Use when writing or reviewing code involving LoRa, TTN, LMIC, join/uplink, deep sleep, or when the user asks about LoRaWAN, TTN, or this hardware stack.
---

# Domain knowledge

## When to apply

- Writing or editing code that uses LoRa, TTN, or LMIC
- Reviewing code or plans that touch join, uplink, sleep, or radio
- User asks about LoRaWAN, TTN, LMIC, Feather M0, or RFM95

## LoRaWAN / TTN basics

- **OTAA vs ABP**: This project uses OTAA (join with AppEUI/DevEUI/AppKey). DevAddr is assigned by the network after join; do not hardcode it. Let LMIC handle the Spreading Factor (SF) hunting during the join phase; do not force a specific SF until *after* a successful join.
- **FPort**: Application port 1–223. This project uses distinct FPorts for DEV vs PROD (e.g. 10 vs 20) for backend routing.
- **Payload size**: LoRaWAN limits uplink payload by region and DR (e.g. 51–222 bytes typical). Keep headers small; batch or compress if needed.
- **Duty cycle**: Sub‑GHz bands are duty‑cycle limited. The project respects LMIC duty cycle and does not manually reset availability timers.
- **Join**: Until joined, `LMIC.devaddr == 0`. Do not sleep during join; run `os_runloop_once()` in a tight, non-blocking loop until joined.

## MCCI LMIC

- **Event loop**: Call `os_runloop_once()` repeatedly. LMIC is non‑blocking; it uses internal OS jobs for MAC timing. **Absolutely no `delay()` calls** are permitted while the radio is active or waiting for RX windows, as this desynchronizes the MAC layer.
- **Clock Error (CRITICAL)**: The Feather M0 uses an internal RC oscillator that drifts. You must relax the clock error to 5%. To prevent a 16-bit integer overflow before division, it must be explicitly cast: `LMIC_setClockError((uint32_t)MAX_CLOCK_ERROR * 5 / 100);`
- **Link Check Mode**: TTN restricts downlinks. If LMIC's default Link Check remains active, the gateway will exhaust its duty cycle sending MAC ACKs. Explicitly call `LMIC_setLinkCheckMode(0);` inside the `EV_JOINED` event.
- **Application jobs**: Do not add application-level OS jobs (e.g. `os_setTimedCallback`) for "wake and send". Use linear flow: wake → read → `LMIC_setTxData2()` → spin until `EV_TXCOMPLETE` → sleep. See master-plan.
- **TX completion**: Transmission is done only after `EV_TXCOMPLETE`. If the device sleeps right after `LMIC_setTxData2()`, the radio may power down before sending. Wait synchronously for `EV_TXCOMPLETE` (e.g. volatile flag + `while(!txComplete) { os_runloop_once(); }`) then sleep.
- **Key APIs**: `LMIC_setTxData2(fport, payload, len, confirmed)`, `LMIC_reset()`, `os_getTime()`, `sec2osticks()`. Credentials via `os_getArtEui`, `os_getDevEui`, `os_getDevKey` (callbacks from PROGMEM).
- **Pin map**: `lmic_pinmap` (nss, rst, dio0, dio1, dio2). This board: nss=8, rst=4, dio=3,6.

## Library traps that have already cost this project

Every one of these was found the hard way. Read before touching the relevant path.

- **`ArduinoLowPower` alarms have ONE-SECOND granularity.** `setAlarmIn()` does `rtc.setAlarmEpoch(now + millis/1000)` — integer division, no sub-second handling. So **`LowPower.idle(750)` sets a zero-second alarm and returns early**. It does not wait. This silently corrupted every PROD temperature reading (the DS18B20 returns the *previous* conversion, so readings are lagged one wake interval) and survived for months because the DEV path uses an `os_runloop_once()` loop instead — **the bug does not exist in DEV**. Never pass a sub-second value to any `ArduinoLowPower` timed call.
- **`ArduinoLowPower` already wraps `RTCZero`.** `deepSleep()` has always been crystal-backed and accurate; adopting `RTCZero` does not improve sleeping, it only makes the clock *readable*.
- **Never call `RTCZero::begin()` on a second instance.** `begin(false)` calls `RTCreset()`, which clears the counter unless `PM->RCAUSE` indicates SYST/WDT/EXT — so a `begin()` during normal operation after a power-on reset **wipes the time**. `_configured` is an instance member, so a second object believes the hardware is unconfigured and reconfigures it. Let `ArduinoLowPower` own configuration (force it once via `attachInterruptWakeup(RTC_ALARM_WAKEUP, NULL, 0)`); use a read-only instance for `getEpoch()` and never `begin()` it.
- **The RTC does not survive `NVIC_SystemReset()`.** SAMD21 has no backup domain. Stash the epoch in `.noinit` before resetting — and do **not** add the pre-reset sleep back on restore; the epoch is read after the sleep, so the RTC already counted it.
- **`LMIC_ENABLE_DeviceTimeReq` defaults to 1** (MCCI LMIC v6.0.1, `src/lmic/config.h`) — nothing to configure. But **the callback carries no time**: `void cb(void*, int flagSuccess)` only reports success. Call `LMIC_getNetworkTimeReference(&ref)` inside it for `{tLocal, tNetwork}`, then add elapsed time since `tLocal` or the clock lands seconds slow. GPS->UTC needs the 315964800 offset plus leap seconds; there is no library helper.
- **An idle `os_runloop_once()` loop starves LMIC's clock.** `os_runloop_once()` calls `lmic_hal_ticks()` only when a job is scheduled; with an empty queue it does nothing. The SAMD HAL extends `micros()` (wraps every 71.6 min) by watching one bit with a 35.8-min toggle period — unsampled longer than that, it can miss a full wrap and set `os_getTime()` BACK 71.6 min. LMIC's duty/channel stamps then sit "in the future" and `engineUpdate` defers the next TX up to that long; with a 2-min app timeout the uplink dies silently, and the failed attempt's stale `LMIC.txend` chains the stall through every later frame that cycle (`engineUpdate` reuses `txend` once `OP_NEXTCHNL` is consumed). Cost gisebo-05 ~2 of 3 hourly cycles overnight 2026-07-27/28 (73% predicted miss rate for a 62-min idle gap — matched). Any long busy-wait must call `os_getTime()` per iteration; PROD deep sleep is immune because `micros()` freezes. Sibling of the `idle(750)` story: one defect per mode, invisible from the other mode's bench.
- **`LMIC_setTxData2()` can refuse and queue NOTHING** — it returns `LMIC_ERROR_TX_BUSY` when `OP_POLL|OP_TXDATA|OP_JOINING|OP_TXRXPEND` is set (`OP_POLL` = LMIC owes the network a MAC-answer uplink, which happens after FPending/confirmed downlinks). Ignore the return and you wait for an `EV_TXCOMPLETE` that never comes — or worse, the autonomous MAC-answer uplink completes, fires `EV_TXCOMPLETE`, and impersonates your frame. Always check the result.
- **`lmic_project_config.h` ships with `CFG_us915` ENABLED**, not with every region commented out. Enabling `eu868` on top defines two regions and LMIC refuses to build. Disable every region, then enable exactly one. `CFG_sx1276_radio` is not a region — it selects the RFM95's chip and stays. That file lives inside the library, so the build config is invisible and unversioned: `reference/lmic_project_config.h` is the known-good copy.

## Hardware (Feather M0 + RFM95)

- **MCU**: SAMD21. `LowPower.deepSleep()` keeps RAM but stops the internal clock; `micros()`/`millis()` do not advance during sleep.
- **RFM95**: SPI, 915 MHz (or region-specific). Needs stable power before SPI: use `delay(5000)` and `SPI.begin()` at the very start of `setup()` before any LMIC calls to prevent blind/deaf radio states.
- **Pin 11 vs 13**: Pin 13 is the onboard LED (pullup leaks current). Use Pin 11 for mode/config (e.g. DEV/PROD); use Pin 13 only as output for visual debug. See master-plan.
- **Battery**: VBAT read via a 100k/100k divider on A7 — 6.45 mV LSB at 10 bits, and a single sample carries ~±19 mV of noise. **Average 16 samples**; a bare reading dithers any threshold it feeds.
- **Battery voltage tracks temperature, and the effect is large.** Measured in production: `gisebo-01` reads **+12.8 mV/°C (r = 0.93)** — alkaline's coefficient — which extrapolates to ~321 mV of drift across a 25 °C season *at constant charge*. This is why the season machine is driven by water temperature and **never** by voltage. The telemetry even identifies the chemistry: alkaline swings with temperature, lithium sits flat (`gisebo-04`, on lithium in a fridge, shows r = −0.24 and −2.5 mV/day at a steady 9 °C).

## Relation to master-plan

The **master-plan** skill encodes this project's design decisions (no FlashStorage, commissioning vs operational, no app OS jobs, respect LMIC duty cycle, etc.). This skill gives the underlying domain so the agent does not suggest approaches that conflict with LoRaWAN or LMIC behavior.

## Additional resources

- See master-plan for design decisions; project docs for setup and payload format.
