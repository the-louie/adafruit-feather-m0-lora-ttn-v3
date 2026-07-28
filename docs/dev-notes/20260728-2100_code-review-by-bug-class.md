# Full-codebase review by bug class

Date: 2026-07-28 21:00 CEST
Scope: every line of our own code — the `.ino` (1125 lines) and all 13 headers
read in full, `decoders/gisebo-05-v7.js` cross-checked field-by-field against
its encoders. Dependencies excluded. Method: derive the *classes* of the bugs
fixed this week from the git history, then hunt each class everywhere.

## The classes (from `fe7b533`..`a2b63c8`)

1. Asymmetric state transitions — set, never undone (`powerSave`)
2. Status sampled at the wrong moment (`success()` after the last read;
   status-byte stale-by-one `e131d33`)
3. Latch whose only clearing path is suppressed (item 23)
4. Guard predicate narrower than the condition it guards (`OP_TXRXPEND` vs the
   real busy set, item 21)
5. Time accounting — fabricated dt (22), starved clock extender (`58e4f74`),
   `idle(750)` truncation
6. Wrapping counters read as absolute facts (`wakeCounter`)
7. Gating on a condition the computation does not use (clarity, 24a)
8. Threshold outside the signal's actual range (the ≥500 mV floor vs a dark
   panel's 0 mV)
9. Unchecked return values (`LMIC_setTxData2`)
10. Encoder/decoder contract drift (season order, `8dc181f`)
11. Comment/code divergence ("~68 ms of averaging")

## Findings — 4, all fixed in this commit

### F1 (class 6, real): data-payload cold-boot flag used a wrapping proxy

`transmitBatchAndWait()` set `STATUS_COLD_BOOT` from `persist.bootCounter <= 1`.
The counter is 3-bit and wraps 7 → 0 → 1, so the **8th and 9th boots of a
session chain re-reported "cold boot"** on the wire despite being warm resets.
This is the wakeCounter lesson in miniature — a wrapping counter read as an
absolute fact — and the exact fact (`g_coldBoot`, "persist was not restored")
already existed. Now used directly.

### F2 (class 1, real): two status-byte flags had no writer at all

`STATUS_SOFT_RESET` (0x02) and `STATUS_TX_TIMEOUT` (0x10) were defined in
`policy_solar.h`, decoded by the TTN formatter — and **never set anywhere**.
Every solar data frame through the entire overnight failure storm reported
`tx_timeout: false`. A defined wire flag nobody sets is worse than none: it
actively asserts health. Now wired: `SOFT_RESET = !g_coldBoot` (each boot is
exactly one of cold/soft, complementary by construction) and
`TX_TIMEOUT = g_txFaultPending` (the pending-unreported-failure latch), so PROD
data frames carry the fault that the out-of-band diagnostic frame may not get
to send for days.

### F3 (class 10/11, comment): `diagnostics.h` restated the season order — backwards

`VerboseSnapshot.seasonState` was commented `"0 Summer, 1 Fall/Spring,
2 Winter"`. `season.h` says `WINTER=0, MID=1, SUMMER=2`. The code passes the
value through untouched, so nothing misbehaves today — but this **identical
inversion already shipped once** as the decoder bug fixed in `8dc181f`, and a
future reader implementing a consumer from this comment reproduces it. Fixed,
with a pointer naming `season.h` as the only authority.

### F4 (class 11, comment): `variant_probe.h` still described the pre-007a46b world

The warm-reset residue is `0x0198` since every cycle began ending in
`powerSave(true)` (MODE bits cleared), not the documented `0x019F`. The
soft-reset handles both identically and the constant stays as the historical
regression vector, but the comment now says so, plus the 2 V POR-threshold
mechanism (why a warm MCU reset never resets the part). CLAUDE.md got the same
correction earlier today; this header was missed then.

## Checked and clean (so the next review need not re-derive it)

- **`payload.h`** — `encodeWaterTemperature(-127)` → RAW_INVALID via the −50
  floor (disconnected ≠ "too cold"); clamps and sentinel ranges match the
  decoder; 12-bit offset + counter-nibble packing verified against
  `decodeUplink`. One observed non-bug: exact half-step temperatures (e.g.
  15.9 °C) can round down one LSB through float slop — quantisation, not logic.
- **`season.h`** — hysteresis directions correct on all four edges; one-level-
  per-call as documented; NaN/−127/>60 °C all hold state.
- **`power_policy.h`** — `voltageOffsetHyst` verified for hold/degrade/improve
  and multi-band jumps; degrade-at-nominal, improve-at-+50 mV as specified.
- **`persist.h`** — header is genuinely 8 bytes on ARM (no padding before the
  body); interior padding bytes are covered by the CRC but never mutated between
  seal and check, so harmless; init/seal/validate/decayed-framed all coherent.
- **`uplink_schedule.h`** — counter stamped before TX, advanced only on success;
  flag-not-counter for the post-join flush; all verified against the decoder's
  +1/repeat/gap reading.
- **`timekeeping.h`** — GPS→UTC offset and leap seconds right; `utcPlausible`
  bounds sane; the `(uint32_t)ref.tNetwork` cast in the `.ino` is safe (GPS
  seconds ≈ 1.4e9). Field evidence agrees: `clock_valid` sets and timestamps
  land, which they could not if the units were wrong.
- **`policy_primary.h` / `policy_solar.h`** — interval arithmetic cannot
  underflow (int math, base ≥ 4, clamp floor); appendPayload clamps every
  field; bonus double-gate as designed.
- **`solar_signal.h`** — reviewed earlier today (items 22, and the dt=0
  contract now pinned by six host cases).
- **`variant_probe.h`** — decision logic sound; the unchecked RST-write return
  is already recorded as TODO 20's judgement call, not re-flagged.
- **`keygen.h`** — HKDF domain separation, EUI shaping (U/L set, I/G clear),
  LSB reversal for LMIC: all correct and host-tested.
- **`diagnostics.h`** — fault computation, encode layouts, and both send
  policies re-verified; byte offsets match the decoder's `decodeVerbose()`
  exactly (0–21, all fields).
- **`decoders/gisebo-05-v7.js`** — every decode offset cross-checked against
  its encoder; out-of-contract bytes reported not dropped (S05-20); season
  indexing defends against value 3; RCAUSE table matches SAMD21 PM->RCAUSE.
- **`.ino`** — setup ordering (probe → policy → persist restore → strap → LMIC),
  join-failure stash-then-reset sequence, restore paths (`currentIntervalIndex`
  0-guard, NaN-safe surfaceTempC), both TX paths post-hardening, DEV/PROD wait
  asymmetries, the sleep-loop clock feed.
- Known accepted items NOT re-flagged: `g_txFaultPending` not persisted
  (deliberate, documented), verbose cadence = max(1 h, interval) (documented),
  sha256.h's C++14 `inline constexpr` build warning (cosmetic).

## Verification

Compiles **73728 B (28%)**; host suite (280 assertions) and decoder suite (39)
both green. F1/F2 are wire-*value* changes on already-decoded bits — no decoder
or formatter change needed, no schema bump. Flash-verify with the rest of the
queue: after the next flash, a warm reset's data frames must show
`soft_reset: true, cold_boot: false`, and a data frame following a failed cycle
must show `tx_timeout: true`.
