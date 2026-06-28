# The fast-flush fires every 16 wakes, and `rebootDetected` never fired (S01-11)

## Summary

Two defects in one place, both proven from production rather than reasoned about.

`wakeCounter == 1` was intended as "the first uplink after joining". The counter is 4-bit, so it **wraps to 1 every 16 wakes** and re-fires with a partial batch. **A third of every uplink ever sent** carries 4 samples and two dead bytes.

`rebootDetected = (sequence === 0)` has **never fired**. The value never reaches 0.

## Evidence — 139 production uplinks, both devices, no exceptions

Full retained TTN window, 2026-07-15 → 07-17.

| seq | samples in batch | gisebo-01 | gisebo-04 |
|---|---|---|---|
| 1 | **4** | 7 | 40 |
| 7 | 6 | 7 | 40 |
| 13 | 6 | 6 | 39 |

Only `{1, 7, 13}`. Never 0. Simulating `loop()` reproduces it exactly: TX at wake 7 (`ramCount` 6), TX at wake 13 (`ramCount` 6), then `wakeCounter` wraps to 1 at wake 17 with only `ramCount` 4 → premature uplink. Period 16 wakes, forever.

## A second, independent confirmation — from timing alone

The sequence values could in principle be explained some other way. The **inter-uplink gaps** cannot:

| device | interval | observed gaps |
|---|---|---|
| gisebo-01 | 30 min (byte 0 = index 4) | **only 120 and 180 min** = 4x and 6x |
| gisebo-04 | 5 min (fixed, V5) | **only 20 and 30 min** = 4x and 6x |

The 6,6,4 wake cycle is visible in the clock, with no reference to the payload contents. Two unrelated lines of evidence agree.

## Why the obvious fix does not work

Moving the increment after the uplink decision makes the boot uplink send 0 correctly — but then the steady-state cycle becomes 0, 6, 12, which *includes* 0. Every third uplink would report a false reboot.

**A free-running 4-bit counter cannot distinguish a reboot from a wraparound in principle.** This is a design fix, not an arithmetic one.

## `rebootDetected` was never in production

Worth stating plainly: the field exists **only** in the repo's `ttn-decoder-v6.js` (now deleted). Neither live formatter contains it. So the defect is real, but nothing was ever misled by it — the field was never deployed.

`doc/test-payloads.md` vector 1 asserts the fast-flush sends `sequence 0` with "Reboot: true". That stale doc records the behaviour the code was *intended* to have and never did, and is how the defect surfaced.

## The reboot signal was free all along

`LMIC_reset()` clears `seqnoUp`, so **every reboot restarts `f_cnt`** — visible in TTN metadata at zero payload cost. Paired with `.noinit` preserving the uplink counter while `f_cnt` resets regardless:

| f_cnt | uplink counter | means |
|---|---|---|
| resets | resets | cold boot (`.noinit` invalid) |
| resets | continues | soft reset (join-failure path) |
| continues | gaps | dropped uplink |

## Fix

- **Explicit `bool firstUplinkAfterJoin`**, set on `EV_JOINED`, cleared after the first successful TX. Never infer "first" from a wrapping counter.
- **The 4 bits become an uplink counter**, incremented once per *successful* TX. Consecutive uplinks then differ by exactly 1: a gap is a dropped message, a repeat is a TX retry.

See S02-04, S02-05. The `{1, 7, 13}` signature remains a useful fleet marker — any unit emitting it is pre-fix.
