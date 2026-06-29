# Dating the temperature lag — and clearing V5 (S01-16, S01-06)

## The defect has a commit, a timestamp, and a 43-minute cover-up

All on **2026-03-09**:

| time | commit | what |
|---|---|---|
| 11:42 | `54f2828` | Add dynamic interval byte to uplink (9-byte payload, decoder v6) |
| 12:01 | `579f935` | ADC: two dummy reads before battery read |
| **12:03** | **`aad7bca`** | **"Dallas conversion: use `LowPower.idle(750)` instead of `delay(750)`"** ← **defect introduced** |
| 12:42 | `3696418` | Remove LMIC duty cycle bypass |
| **12:46** | **`c754a43`** | **"Conditional LowPower.idle in readAndBufferSensors for DEV USB stability"** ← **workaround, 43 min later** |

**The code was correct before 12:03.** It read:

```c
sensors.requestTemperatures();
delay(750); // Dallas sensors need time to convert at 12-bit resolution
float tempC = sensors.getTempCByIndex(0);
```

`aad7bca` replaced that working `delay(750)` with `LowPower.idle(750)` as a **power optimisation**. It saved nothing measurable — quiescent draw (~290 µA) dominates the budget — and silently corrupted every PROD reading taken since.

## The workaround destroyed the evidence

Within **43 minutes** the defect produced a loud, visible symptom: USB dropping on every measurement in DEV (`idle()` gates the APB clock). `c754a43` diagnosed that as a USB/clock stability problem and fixed it **for DEV only** — replacing `idle(750)` with an `os_runloop_once()` loop there, and leaving `idle(750)` in PROD.

That workaround was correct about the symptom and never suspected the cause. And it removed the **only visible evidence** the change had broken anything. From 12:46 onward the defect was invisible: DEV worked because it no longer used `idle()`, and PROD had no serial to complain with.

This is why the bug survived four months. Not because it was subtle — because the one thing that noticed it got silenced.

## V5 is clear — TODO #14's open question, answered

The 8-byte payload predates `aad7bca` entirely. Any V5-era firmware uses `delay(750)` and **is not lagged**.

**`gisebo-04` is therefore trustworthy.** Its temperature history needs no correction and no caveat. That closes S01-06 and removes the "gisebo-04's entire history is uninterpretable" worry from TODO #14 — the missing V5 source no longer matters for data integrity, only for maintenance.

Which is fortunate: gisebo-04 is the fridge unit running the cold lithium test, and its data is currently the project's only real evidence on primary-cell discharge behaviour.

## gisebo-01: lagged, but the start date is unprovable

`gisebo-01` sends 9-byte payloads, and the repo's v6 firmware contains `idle(750)`. So it is lagged — **if** it was flashed from code committed after 12:03 on 2026-03-09.

There is a narrow window (11:42–12:03) where 9-byte firmware still had `delay(750)`. Nothing in telemetry distinguishes lagged from unlagged data without an independent temperature reference, and TTN retains only ~3 days, so the flash date cannot be recovered from uplinks either.

**Practically:** treat all of gisebo-01's 9-byte history as one interval (30 min) late. The 21-minute window where it might not be is not worth reasoning about.

## Decision: annotate, do not correct

Per 2026-07-17. Rewriting a real historical series risks corrupting it, and TTN's ~3-day retention means the bulk already lives in influx and would have to be rewritten in place. S08-05 adds:

- an annotation at the cutover moment;
- a standing note across gisebo-01's whole span: **samples are 30 minutes later than the water they measured**.

Anyone correlating this series against weather, gisebo-04, or gisebo-05 needs to know before drawing a conclusion.

## The fix is a revert

S02-01 chose `delay(750)`. That is precisely `aad7bca` undone. The original code was right; the optimisation was the bug.

---

## Addendum (S01-05): the V5 source was never missing

An earlier claim — repeated through `TODO.md` item 14 and several sprint tasks — was that "gisebo-04 runs firmware whose source is not in this repo". **That was wrong, and it was my error**: I checked HEAD, not history.

**V5 is `1f6afc9` (2026-02-22), now tagged `v5-firmware`.** It is the last commit before `54f2828` introduced the 9-byte payload.

Confirmed three independent ways:

1. **Source:** `static uint8_t payload[8]`, `LMIC_setTxData2(currentFPort, payload, 8, 0)`, and the layout `payload[0] = uoffset >> 4; payload[1] = ((uoffset & 0x0F) << 4) | (wakeCounter & 0x0F);` — exactly what gisebo-04's live decoder reads.
2. **Behaviour:** `sleepIntervalSeconds = 300;` hardcoded, with **no** `kIntervalSecondsByIndex` and no `calculate_interval_index`. A fixed 5-minute interval, no dynamic algorithm — which independently explains the 5.03 min wake interval measured from gisebo-04's real inter-uplink gaps.
3. **Decode:** feeding a real gisebo-04 uplink (`f_cnt=709`, bytes `[139,151,95,95,95,95,95,95]`) through that layout yields battery **5.233 V**, sequence **7**, temps **9.0 ×6** — byte-for-byte what TTN reported.

It also contains `delay(750)` and zero `LowPower.idle`, which is the third confirmation that **V5 is not lagged**.

**Item 14 is closed.** The fleet does run two firmware versions, but that is now a benign, fully understood fact rather than an unknown: gisebo-01 on v6 (lagged, frozen, retired at cutover), gisebo-04 on `v5-firmware` (clean, fridge test, untouched), gisebo-05 on v7 (new).
