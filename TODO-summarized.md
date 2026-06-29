# TODO — summarized

One-line index of `TODO.md`. Format: `title - complexity - estimated time - summary`.
Tasks are broken down under `docs/sprints/sprint-NN/`.

1. **`LowPower.idle(750)` does not wait — PROD temperature data is corrupt** - Low to fix - 6–9 h - *Confirmed from library source and field data.* `setAlarmIn` truncates `750/1000` to a zero-second alarm, so the DS18B20 read returns the previous conversion: every PROD reading is lagged one wake interval. Both units transmit, so it returns early rather than hanging. Sprint 01 (investigate), 02 (fix), 06 (verify on a PROD-strapped unit — never DEV).
2. **The fast-flush fires every 16 wakes, and `rebootDetected` has never worked** - Low - 4–5 h - *Confirmed from 107 production uplinks.* `wakeCounter` is 4-bit and wraps, so `wakeCounter == 1` re-triggers the fast-flush with a partial batch — sequence takes only {1, 7, 13}, and a third of all uplinks ever sent carry 4 samples and two dead bytes. Sequence is never 0, so the reboot flag cannot fire. Give the fast-flush its own flag; make the 4 bits an uplink counter. Sprint 02.
3. **Regenerate test payload vectors** - Low - 5–7 h - *Blocked on items 2, 8, 9, 13.* `doc/test-payloads.md` documents the 8-byte V5 payload; all four vectors fail the decoder's length check and validate nothing. Rewrite as `docs/test-payloads.md` covering the 9-byte primary and 15-byte solar payloads, executed in a Node harness. Sprint 01 (harness), 05 (vectors).
4. **Split firmware into core + `PowerPolicy` architecture** - Medium - 14–18 h - Runtime I2C probe selects `PrimaryCellPolicy` or `SolarPolicy` behind a virtual interface; season machine shared, not duplicated. `PrimaryCellPolicy` must be behaviourally identical — production confirms the current algorithm works. Sprint 02.
5. **Persistent state across reset (`.noinit`)** - Low–Medium - 5–7 h - A soft reset doesn't clear SRAM, only the C runtime does, so `.noinit` survives `NVIC_SystemReset()` at zero flash cost. Magic word plus layout version; version mismatch must force a cold boot. Sprint 03.
6. **RTC, wall clock, and `DeviceTimeReq`** - Medium - 8–11 h - `ArduinoLowPower` already wraps `RTCZero`, so sleeping was always crystal-backed; this makes the clock *readable*. One `DeviceTimeReq` at join holds for months. The RTC does not survive a system reset — stash the epoch in `.noinit`. Sprint 03.
7. **Day length and solar expectation** - Low–Medium - 4–6 h - `clarity = ewma / expected_daylight_fraction` separates weather (and panel fouling) from season, at zero payload cost since the backend has the timestamp and latitude. Season stays on water temperature — day length was considered and rejected. Sprint 03.
8. **Solar policy and INA219** - Medium–High - 16–20 h - Li-ion bands 3.85/3.65/3.45; bonus only at `voltage_offset == 0`, floor index 2. Keys on panel *bus voltage*, not current — current collapses to ~0 when the pack is full, which is most of the summer. Time-based EWMA window to prevent multi-day hunting. Sprint 04.
9. **Decoder v7** - Medium - 8–11 h - 15-byte payload on FPorts 11/21, solar field parsing, clarity ratio, `uplink_counter` replacing the dead `rebootDetected`. Sprint 04.
10. **Backend alarms and monitoring** - Low - 5–7 h - Uplinks are the only instrument. Probe misdetect (FPort 10 + battery <4.5 V) is the highest-value one — without it a loose INA219 connector silently parks a unit at a 7-day interval. Sprints 01, 05.
11. **Hardware BOM decisions** - Research - 3–5 h remaining - *Partially resolved.* **No MPPT — decided 2026-07-17**: no energy case against ~10× surplus, the healthy-battery gate covers the signal risk, and the INA219's 128-sample averaging mitigates the measurement risk. First order placed (Feather, DS18B20, INA219, panel, pack). Supercap rating and over-discharge protection still open. Sprints 01, 07.
12. **On-device verification** - Medium - 10–14 h - Hardware arrives later than sprint 03, so sprints 02–05 ship verified by compilation and host tests alone. Sprint 06 (core) and 07 (solar) verify retroactively, each with a 4 h remediation buffer.


---

## Summary

12 open items. Item 11 is partially resolved. **Items 13 and 14 are DONE.** **Item 13 (the repo decoder is not what runs) is DONE** — both live formatters exported to `decoders/`, diffed, and the stale `ttn-decoder-v6.js` deleted. Planned across 7 sprints in `docs/sprints/` (115 tasks, 1 developer, ~30 h per sprint).

**Two confirmed defects lead the list**, both proven from the 2026-07-16 production capture rather than from reasoning. Item 1 means every PROD temperature reading is one interval late — plausible-looking and silently wrong. Item 2 means a third of all uplinks ever sent carry a short batch, and the reboot flag has never once fired. Neither was found by testing, because nothing executable ever checked the firmware↔decoder contract — which is what item 3 exists to fix, and why item 3's harness is scheduled before the vectors it will run.

**Item 14 is closed, and its premise was false.** V5's source was never missing — it is at `1f6afc9`, tagged `v5-firmware`. Confirmed three ways: it contains `delay(750)` and no `LowPower.idle` (so **not lagged**); `sleepIntervalSeconds = 300` hardcoded with no dynamic-interval code (which independently explains gisebo-04's measured 5.03 min gap); and decoding a real gisebo-04 uplink with its layout reproduces TTN's output exactly. gisebo-04's data is trustworthy.

**The plan's largest risk is unverified firmware.** Hardware arrives later than sprint 03, so sprint 04 picks the index-2 floor on a per-wake energy figure whose two estimates disagree by 35×, and sprint 03 ships an RTC ownership seam nobody can test. Sprints 06–07 verify retroactively and carry remediation buffers. With two units in the field and no bench, the first board to receive new firmware is a production board on a post at a lake.
