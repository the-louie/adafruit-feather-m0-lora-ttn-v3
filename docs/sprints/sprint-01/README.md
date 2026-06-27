# Sprint 01 — Establish ground truth

**Dates:** 2026-07-16 (Thu) → 2026-07-29 (Wed)
**Capacity:** 1 developer, ~30 h effective
**Planned:** 19 tasks, ~29 h

## Goal

Nothing in this codebase can be trusted until we know what is actually deployed. Sprint 01 writes no firmware. It establishes what the fleet runs, what the decoder is, records the two confirmed defects with their evidence, and closes out the hardware questions — all desk work, which suits the fact that **hardware has not arrived**.

## Why this order

The 2026-07-16 TTN capture (`docs/dev-notes/real-world-data__20260716.json`) proved three things that invalidate assumptions the rest of the plan rested on:

- The decoder in the TTN console is **not** `ttn-decoder-v6.js`. Writing test vectors against the repo file would validate an artifact nobody runs. → tasks 01–04.
- The fleet runs **two protocols**: `gisebo-01` on 9-byte v6, `gisebo-04` on 8-byte V5 whose firmware source is not in this repo. → tasks 05–07.
- `LowPower.idle(750)` does not hang — both units transmit — so the **early-return branch is confirmed** and the one-interval temperature lag is live. → tasks 10, 16.

Tasks 13–15 close out hardware. **No MPPT — decided 2026-07-17** — no external charger; task 12 records why. The supercap and protection questions remain open and gate the second order.

## Working agreement

- **Commit in small batches**: one function + its test (if available) + its documentation, per commit. Several commits per task, never one commit per task.
- Every non-trivial change gets a dated dev-note in `docs/dev-notes/`.

## Task index

| # | Task | Est | Item |
|---|---|---|---|
| **00** | **Prove the project still builds** | **1–2 h** | **—** |
| 01 | Export the live TTN decoder | 1–2 h | 13 |
| 02 | Diff live decoder against repo copy | 1–2 h | 13 |
| 03 | Reconcile or delete the repo decoder | 1 h | 13 |
| 04 | Node test harness for the decoder | 2 h | 3 |
| 05 | Identify gisebo-04 firmware and locate V5 source | 2 h | 14 |
| 06 | Establish whether V5 shares the idle 750 defect | 1–2 h | 14 |
| 07 | Fleet reflash plan | 1–2 h | 14 |
| 08 | Backend alarm 252 in slot 0 pre-fix baseline | 1–2 h | 10 |
| 09 | Backend alarm FPort 10 with battery below 4.5 V | 1–2 h | 10 |
| 10 | Dev-note idle 750 confirmed defect | 1–2 h | 1 |
| 11 | Dev-note fast-flush misfire and dead rebootDetected | 1–2 h | 2 |
| 12 | Record the no-MPPT decision | 1 h | 11 |
| 13 | Spike supercapacitor part selection | 1–2 h | 11 |
| 14 | Spike over-discharge protection | 1–2 h | 11 |
| 15 | BOM and procurement request | 1–2 h | 11 |
| 16 | Quantify historical impact of the lag | 2 h | 1 |
| 17 | Update CLAUDE.md with confirmed findings | 1–2 h | — |
| 18 | Update cursor skills with confirmed defects | 1–2 h | — |

## Do task 00 first

Nothing else in this plan matters if the project does not compile, and **nobody has checked**. The only verification sprints 02–04 have is "it compiles"; `lmic_project_config.h` lives inside the library rather than the repo, so the build config is invisible and unversioned. If it cannot be reproduced from a clone, sprint 02 stalls on day one.

## Exit criteria

- **The project compiles from a reproducible environment**, with library versions pinned and a flash/RAM baseline recorded.

- The real decoder is committed and is the only decoder in the repo.
- We know what `gisebo-04` runs, and whether its history is interpretable.
- Both confirmed defects are written up with their production evidence.
- The no-MPPT decision is recorded with its rationale and its reopening condition.
- Supercap and protection are decided and ordered.
- Two backend alarms are live, including the 252 baseline **before** any fix ships.
