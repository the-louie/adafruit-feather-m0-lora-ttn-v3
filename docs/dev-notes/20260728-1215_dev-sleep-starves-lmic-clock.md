# DEV sleep loop starves LMIC's clock extender — the overnight TX timeouts

Date: 2026-07-28 12:15 CEST
Fixes the defect documented in `20260728-1100_overnight-gisebo-05-tx-timeouts-and-frozen-ina219.md` §2.

## Root cause (read from source, all four links)

1. **`lmic_hal_ticks()`** (`MCCI .../src/hal/hal.cpp`) builds the 32-bit tick
   counter from `micros() >> 4`, which is only 28 bits and wraps with `micros()`
   every **71.6 min**. It extends it with a static `overflow` byte that watches
   bit 27 — a bit that toggles every **35.8 min**. The detector compares the
   current bit against the stored copy, so it must be called at least once per
   35.8 min. An unsampled gap in which the bit toggles TWICE (0→1→0) leaves the
   bit equal, the wrap goes uncounted, and `os_getTime()` comes back **71.6
   minutes behind** continuous time.

2. **`os_runloop_once()`** (`oslmic.c`) calls the clock **only when a job is
   scheduled** (`OS.scheduledjobs && lmic_hal_checkTimer(...)`). With an empty
   queue it falls through to `lmic_hal_sleep()` — a no-op on SAMD. So an idle
   LMIC never samples its own clock, no matter how hot the loop spins.

3. **The DEV sleep is exactly that**: ~60 min of `os_runloop_once()` with an
   empty queue (after the last TX of a cycle, engineUpdate schedules nothing).
   For a ~62 min unsampled gap, the probability that bit 27 toggles twice is
   (62 − 35.8)/35.8 ≈ **0.73 per cycle** — the overnight capture lost ~2/3 of
   cycles. The phase advances deterministically each cycle, producing the
   observed fail-runs of 2–3 rather than independent coin flips.

4. **Why a lost 71.6 min stalls the TX**: LMIC's throttle stamps
   (`bands[].avail`, `txend`, `globalDutyAvail`) were written during the
   previous cycle, ~62 real minutes ago. On the rewound clock they now sit up to
   ~10 min **in the future**, so `engineUpdate` dutifully schedules the TX for
   then (`txdelay:` → `os_setTimedCallback`). Our 2-min wait times out first and
   `LMIC_clrTxData()` cancels the job. Follow-up frames in the same cycle are
   worse: the failed attempt consumed `OP_NEXTCHNL`, so they reuse the stale
   `LMIC.txend` (`engineUpdate`: `else txbeg = LMIC.txend;`) and burn 2 min
   each — the exact 2-min-per-frame arithmetic in the capture. The next cycle
   recovers if wall-clock has caught up, or fails again if that gap missed
   another wrap.

**PROD is immune by construction**: `LowPower.deepSleep()` freezes `micros()`,
so a PROD sleep contains no elapsed microseconds to lose, and the awake windows
are short and densely sampled. This is the mirror image of the `idle(750)`
defect (PROD-only because DEV used a different wait) — each mode has now grown
one defect the other mode cannot exhibit. Bench-verify accordingly.

## Fix

One line plus a comment in the DEV sleep loop: call `(void)os_getTime()` every
iteration. That keeps the extender sampled at kHz rate against a 35.8-min
requirement. No library change, no application OS jobs, no duty-cycle state
touched.

Hardening in the same change set (separate commit):

- **`LMIC_setTxData2`'s result is now checked** in `transmitBatchAndWait()` and
  `txFrameAndWait()`. A refusal (`LMIC_ERROR_TX_BUSY` while the stack owes the
  network a MAC answer, etc.) queues nothing and can never produce
  `EV_TXCOMPLETE`; the old code burned the full 2-minute wait — and an
  autonomous MAC-answer uplink completing during that window would have set
  `txComplete` and passed off as our frame.
- **`logTxSchedState()`** (DEV-only): prints `LMIC.opmode` and the
  now-relative age of every throttle stamp at each TX attempt and timeout. If
  any scheduling stall ever recurs, one serial line names the culprit; large
  positive deltas right after an idle hour are this defect's signature.
- **TX faults are latched for diagnostics** (`g_txFaultPending`), because the
  overnight data proved the old `lastTxTimedOut` flag could never reach the
  air: the fault frame carrying it was dropped by the same failure, and the
  next successful data uplink cleared it before the next diagnostic
  evaluation. The latch clears only after a diagnostic frame transmits.
  `DIAG_FAULT_TX_TIMEOUT` (bit 0x0020, unchanged on the wire) now means "an
  uplink failed since the last successful diagnostic report".

## Verification

- Compiles: 73548 bytes (28%). Host suite + decoder suite all green (nothing
  wire-visible changed; the latch is `.ino` glue feeding the existing
  host-tested `diagShouldSend`/`diagMarkSent`).
- Over the air, after flashing: the failure signature is a **~60 min verbose
  cadence with no multi-hour gaps**. Before the fix the expected miss rate was
  ~73%/cycle; a clean 6-hour stretch of hourly frames (P < 0.05% under the old
  behavior) confirms the fix.
- If a stall ever recurs, the `data TX:`/`oob TX:` serial lines carry the
  throttle stamps to diagnose it in one look.
