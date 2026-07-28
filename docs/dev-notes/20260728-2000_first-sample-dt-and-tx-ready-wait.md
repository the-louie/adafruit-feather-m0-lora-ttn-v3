# The first sample credits no time; the TX path gets a bounded wait

Date: 2026-07-28 20:00 CEST
Closes TODO items **22** and **21**. Both are `.ino` changes that land at the
next flash.

---

## 1. The first sample after every boot fabricated an interval of `dt` (item 22)

`readAndBufferSensors()` fed the solar policy the interval it *just slept*:

```c
solarPolicy.ingestSample(busMv, currentMa, sleepIntervalSeconds);
```

Correct for every cycle but the first. `setup()` fills `sleepIntervalSeconds`
from the restored (or default) interval index **before any sleep has happened**,
so the opening sample of every boot credited elapsed time that never elapsed.

- **Sun EWMA:** negligible — α = 1 − exp(−3600/86400) = 0.041, applied once.
- **Harvest accumulator:** material — it integrates `current × dt` directly. A
  warm reset at index 5 in good sun credits `34.5 mA × 1 h ≈ 34 mAh` that never
  flowed. That is **more than a typical full day's harvest** (7–28 mAh/day at
  energy balance), landing on top of the `.noinit`-restored running total.

Already visible on the wire: the 2026-07-28 14:06:55 cold boot reported
`harvest_mah: 2`, which is exactly `34.5 mA × 300 s` (index 2, the cold-boot
default) = 2.875 mAh floored by the accumulator. Harmless at five minutes; the
same defect at a restored 60-minute index is twelve times larger. **PROD is the
worse case** — the join-failure `NVIC_SystemReset()` re-enters `setup()` with a
*restored* index, not the short cold-boot default.

**Fix.** A `firstSampleAfterBoot` flag selects `dt = 0` for the opening sample.
Both primitives already behave correctly at zero — `sunEwmaUpdate()` has an
explicit `if (dtSeconds == 0) return ewma;` guard and `harvestAdd()` accrues
`currentMa × (0/3600)` = 0 — so this is a guard in the glue, not a change to any
host-tested primitive. The flag is cleared at the **end** of
`readAndBufferSensors()`, not inside the solar branch, so it means "the first
sensor read of this boot" regardless of variant and a primary board cannot leave
it armed for a policy it never runs.

**Why zero rather than the true elapsed time.** The device genuinely *was* off
for some period — the 15-minute PROD join-failure sleep, a flash, a power cut —
and the panel may well have been charging through it, so zero under-counts. But
that duration is not knowable from `millis()`, which does not advance through
deep sleep or across a reset, and a fabricated value is indistinguishable from
real harvest downstream. Under-counting something unmeasurable beats inventing
it. The RTC could supply it (`persist.rtcEpoch` → `rtc.getEpoch()` when
`clockValid`); deliberately not taken, as that adds a clock dependency to a path
that must work before the clock is valid.

**Tests.** New host cases in `test_policy_solar.cpp` pin the contract the glue
relies on: `dt = 0` leaves the EWMA, the harvest total and the latched bonus
unchanged; fifty consecutive zero-dt samples still bank no sub-mAh remainder (or
repeated boots would drift the accumulator upward a fraction at a time); and the
live bus/current readings are **still recorded**, so a boot's first frame still
reports real panel state while crediting no time.

---

## 2. Only one out-of-band frame got through per cycle (item 21)

`loop()` sends up to three frames per cycle — data, fault, verbose — each
through its own blocking call. Every uplink draws a downlink from TTN, after
which LMIC owes the network a MAC answer and sets `OP_POLL`. And
`LMIC_setTxData2()` refuses to queue while the TX path is busy:

```c
LMICJ_isTxPathBusy() = (LMIC.opmode & (OP_POLL | OP_TXDATA | OP_JOINING | OP_TXRXPEND)) != 0
```

So the first frame of a cycle went out, provoked a downlink, and the rest were
refused. Observed twice on gisebo-05 on 2026-07-28:

| cycle | data | fault | verbose |
|---|---|---|---|
| boot 14:06:55 | sent | deferred | deferred |
| 15:07:00 | — | sent | deferred |

The payload-less frames at `14:07:01/:07/:12/:18` are those MAC answers.

**Why the old guard missed it.** `txFrameAndWait()` and `transmitBatchAndWait()`
tested `OP_TXRXPEND` only — but `OP_POLL` is a *different bit*. The frame sailed
past the guard and was refused one level down at `setTxData2()`. That refusal
was caught and reported only because of the return-value check added this
morning in `58e4f74`; before that it would have burned the full 120-second wait
and reported nothing.

It is a **delay, not a loss** — `bootDiagSent`/`verboseSentOnce` are only set on
success — and in steady state, with one frame due per cycle, everything gets
through. Which is exactly why it never surfaced before: it bites only when
several frames coincide, i.e. at boot. And that is what makes it matter: in PROD
the retry waits a whole sleep interval, **up to 7 days at index 10**, on the
once-per-boot "I am alive, here is my reset cause" frame — on precisely the unit
that most needs to send it.

**Fix.** `waitForTxReady()` spins `os_runloop_once()` until LMIC will accept a
frame or a 30 s budget expires, replacing the instant bail in both TX paths. The
MAC ping-pong runs at the ~6 s EU868 1% pacing, so the budget covers about five
exchanges — the observed boot burst was four over 17 s.

It tests **`LMIC_queryTxReady()`**, the library's own `!LMICJ_isTxPathBusy()`,
rather than hand-picking opmode bits. Hand-picking bits is precisely how the old
guard drifted out of step with the library, and the same mistake is now
impossible to repeat without deleting the call.

Exhausting the full budget is *not* routine contention — it means the stack is
wedged — so that path sets `g_txFaultPending`, unlike the old instant defer
which reported nothing. Ordinary "wait a moment" contention now resolves inside
the budget and raises no fault at all.

**Cost.** Up to 30 s of awake time per deferred frame, and only when the stack is
actually busy. In PROD the verbose frame does not exist, so the worst case is one
30 s wait for the fault frame — against the multi-day delay it replaces.

**Not host-tested:** the decision is "is LMIC ready yet", which is LMIC state, not
judgement. Verified by compilation and over the air.

---

## Verification

- Compiles: **73704 bytes (28%)**.
- Host suite: **280 assertions, all passing** (was 274 — six new for `dt = 0`).
- Decoder suite: **39 passed, 0 failed**.
- Over the air, after the next flash:
  - **Item 22** — a boot's first solar frame must report `harvest_mah` equal to
    the pre-reset value (warm reset) or `0` (cold boot), not that plus an
    interval's worth.
  - **Item 21** — a boot must land **all three** frames (data, fault, verbose)
    within one cycle instead of one per cycle. Verify at a **boot**; the failure
    is invisible in a healthy steady state.

**Not flashed.** gisebo-05 is still verifying the 2026-07-28 morning binary
overnight. These queue with items 19 and 23 for the next flash.
