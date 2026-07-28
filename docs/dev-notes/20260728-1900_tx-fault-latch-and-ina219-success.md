# Two follow-up fixes to the code shipped this morning

Date: 2026-07-28 19:00 CEST
Closes TODO items 23 and 19. Both are corrections to code that shipped in
`58e4f74` / `007a46b` earlier today; both are `.ino` glue, compile-verified,
with the behaviour verified over the air after the next flash.

## 1. The TX-fault latch deadlocked against its own rate limiter (item 23)

`g_txFaultPending` means "an uplink failed and no frame has reported it yet". It
was cleared in exactly one place — after a successful **diagnostic** frame. But
`diagShouldSend()` refuses to send a diagnostic frame for a bit already latched
into `persist.diagLastSentFaults`, and the periodic re-alert is capped at
`DIAG_MIN_RESEND_SECONDS` (24 h). So the only thing that could clear the latch
was suppressed precisely because the fault had once been reported.

Caught on gisebo-05 today:

| time | event |
|---|---|
| 14:07 | boot-cycle fault + verbose frames refused (`OP_POLL`, item 21) → latch set |
| 15:07 | diagnostic frame sent, carried `tx_timeout`, latch cleared… then the verbose frame was refused → **latch set again** |
| 16:07, 17:07, 18:07 | verbose frames succeed on an exact hourly cadence, each still reporting `tx_timeout` / `healthy: false` |

Nothing had failed since 15:07. It would have read unhealthy until the 24 h
re-alert the next afternoon. Worse than the cosmetics: a single old failure and
an hourly recurring one produced identical telemetry — the inverse of what the
latch was added for.

**Fix.** The verbose frame already carries the full fault bitmap
(`gatherVerbose()` → `diagComputeFaults()`), so when it transmits the fault
*has* reached the air. Clear the latch there too:

```c
if (txFrameAndWait(VERBOSE_FPORT_DEV, payload, DIAG_VERBOSE_LEN)) {
  lastVerboseMillis = millis();
  verboseSentOnce = true;
  if (v.faults & DIAG_FAULT_TX_TIMEOUT) g_txFaultPending = false;
}
```

Ordering is safe by construction: `gatherVerbose()` snapshots the faults *before*
the TX, so a frame that succeeds provably carried the bit, and a frame that fails
re-arms the latch inside `txFrameAndWait()`.

**DEV-only in effect**, since the verbose frame does not exist in PROD. That is
the right asymmetry: DEV gets per-occurrence resolution for bench work, PROD
keeps the once-per-day limit that protects duty cycle and battery. The
`diagMarkSent()` rate limiting is untouched — it is the intended spam-proofing,
not a bug.

## 2. `ina219.success()` was sampled only after the last read (item 19)

`Adafruit_INA219::success()` returns `_success`, which **every accessor
overwrites** with the result of its own register read. This morning's fix wrote:

```c
uint16_t busMv = ...getBusVoltage_V()...;   // sets _success (bus read)
float currentMa = ina219.getCurrent_mA();   // OVERWRITES it (current read)
g_ina219ReadOk = ina219.success() && (busMv < 20000);
```

So a **failed bus read followed by a successful current read reported healthy**,
and `busMv` carried whatever the failed read left behind — which then fed
`sunPresent()` and the EWMA. The reasoning behind the check was right (a voltage
floor would false-fault every night on a dark panel); the sampling point was
wrong. Now sampled after each accessor and ANDed.

## Verification

- Compiles: **73588 bytes (28%)**. Host suite and decoder suite both green.
- Neither change is host-tested: both are single state assignments in `.ino`
  glue, and the judgement they encode lives in already-tested primitives
  (`diagShouldSend`/`diagMarkSent`, `diagComputeFaults`). Stating that plainly
  rather than manufacturing a test that only re-asserts an assignment.
- Over the air, after the next flash: `tx_timeout` must appear on **exactly one**
  verbose frame after a refused frame and be absent from the next, instead of
  persisting for 24 h. The `success()` change is a no-op on a healthy unit —
  which is the point.

**Not flashed yet.** gisebo-05 is mid-verification on the 2026-07-28 morning
binary; the overnight run confirming the clock and INA219 fixes must complete
first. These land in the next flash together with whatever else is queued.
