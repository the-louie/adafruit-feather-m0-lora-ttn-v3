# Code review, the unflashed queue (58e4f74..HEAD)

Date: 2026-07-29 10:13 CEST
Scope: every line changed since the binary currently running on gisebo-05, which is ten firmware commits and three decoder commits, roughly 1160 diff lines across the sketch, seven headers, the decoder, and the test suites.
Method: three passes as specified, a pattern scan, a logic and edge case pass, and a downstream impact pass, followed by a comment quality audit.

## Summary

The queue contains items 18 through 26: the CNVR gated INA219 read with the OVF fault, the TX fault latch corrections, the bounded TX ready wait, the first sample dt guard, the two arm sun predicate, the probe identity extension, verbose schema 2, and the clarity convergence gate. The logic in the pure headers is well tested, 332 host assertions and 53 decoder tests pass, and the wire formats are versioned correctly. The review found one genuine defect introduced this morning, one DRY violation in the I2C glue, one misleading decoder warning, and a number of comments that described the history of the code rather than the code. All four were remediated in this pass, verification below.

## Critical findings

| Issue | Severity | Description | Suggested Fix |
| :--- | :--- | :--- | :--- |
| Verbose retry storm | High | `evaluateAndMaybeSendVerbose()` is now called from every iteration of the DEV sleep loop, but its due time (`lastVerboseMillis`) advances only on a successful transmit. After a failed attempt the frame stays due continuously, and each attempt can block for the full 30 second `waitForTxReady` budget, so a wedged radio stack converts the entire sleep window into back to back 30 second blocking waits. The pre-existing once per wake cadence never had this exposure, it was introduced with the sleep loop emission. | Add an attempt spacing gate, distinct from the due gate, with a five minute backoff between failed attempts. Implemented as the pure `verboseRetryAllowed()` in `diagnostics.h` with wrap safe unsigned arithmetic, plus glue in the sketch. |
| Duplicated wake sequence | Medium | The wake, CNVR poll, and read sequence existed twice, once in `readAndBufferSensors()` and once in `samplePanelForStats()`, with subtly different failure handling. Divergence between two copies of exactly this kind of hardware sequence is how the powerSave defect survived, one copy fixed and one copy forgotten is a plausible future. | Factor a single `ina219WakeForReading()` helper owning the wake and the CNVR poll, callers keep their own failure semantics and the `powerSave(true)` restore. Implemented. |
| Misleading schema warning | Low | `decodeVerbose()` warned `decoding as v2` for any unknown schema, but a schema 0 frame at 22 bytes actually decodes through the v1 path. A wrong diagnostic message in the tool used to debug wire problems costs real time. | Compute the claimed version from the same rule the length check uses. Implemented, with a test asserting the honest warning. |
| Changelog comments | Low | Several comments described what changed and when rather than what the code does, for example the removed variable note in the sketch, a review date tag in the status flag block, and the inversion history in a test. A reader needs the concept, the history lives in git and the dev notes. | Rewrote the affected comments to explain the concept, keeping incident references only where they carry the reason the code is shaped as it is. Implemented across four files. |

## Findings accepted without change

These were examined and judged correct as written, recorded so the next review does not rediscover them.

1. `gatherVerbose()` reads `surfaceTempC` and policy state that are stale by up to an hour when the frame is emitted mid sleep. This is snapshot semantics, the frame reports the last wake's state plus live counters, and the alternative, reading sensors out of cycle, would perturb the dt accounting.
2. The battery is sampled by three separate ADC bursts per cycle (ingest, payload build, diagnostics gather). The cost is microseconds against a 750 ms window, and sharing one sample across the three would couple functions that are otherwise independent.
3. `clarity_converging` is absent rather than false when `recvTime` is missing on a schema 2 frame. The field only means something when clarity could have been computed at all.
4. A wedged verbose attempt still latches `g_txFaultPending` on every failed attempt. The latch is idempotent, and the fault frame rate limiting sits downstream, so no spam results.
5. The panel stats sampler skips failed samples silently. The profile is a diagnostic, not a control input, and the wake time read path already raises the fault for a persistently failing part.

## Downstream impact assessment

1. The data payload bytes and FPorts are unchanged by the entire queue, gisebo-01 and gisebo-04 are unaffected.
2. Schema 2 verbose frames and the new decoder fields (`uptime_s`, `cycle_count`, `panel_ma_min` and friends, `clarity_converging`, `ina219_ovf`) flow through the application wide TTN webhook into the telegraf pipeline. The recipient discards unknown fields today, but the cutover migration must account for the enlarged schema. Recorded in `TODO.md` item 10 rather than left in this report.
3. Schema 1 verbose captures continue to decode, proven by the untouched live vector in the suite. The formatter was re-uploaded after the warning fix and verified byte identical.
4. `ProbeResult` gained two fields, all constructors were updated, and the aggregate initialisers in the test suite were extended in the same commit, so no positional initialiser drift remains.

## Remediation log

All four findings were fixed in this pass, each with the reasoning above. Verification: firmware compiles at 74572 bytes (28 percent), the host suite passes 332 assertions including four new retry gate cases, the decoder suite passes 53 including the honest warning case, and the formatter on TTN is byte identical to the repository file. No finding required deferral, the only TODO.md addition is the cutover schema note attached to item 10.
