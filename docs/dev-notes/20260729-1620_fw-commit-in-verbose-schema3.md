# Verbose schema 3: the firmware commit on the wire, stamped by the build

Date: 2026-07-29 16:20 CEST
Operator-requested. Landed as one contract change (`diagnostics.h` + `.ino` +
decoder + formatter, per the rule), plus `scripts/build.sh`.

## What

Bytes 34-36 of the verbose frame carry the first 6 hex chars (24 bits) of the
git commit the running binary was built from. The decoder emits
`fw_commit: "b4f1fa"` — or `null` when the field is 0x000000, the deliberate
"unofficial build" marker for anything compiled outside the script. 24 bits is
collision-safe for this repo's lifetime (300 commits ≈ 0.3% birthday odds, and
a collision costs one extra `git rev-parse` disambiguation, not correctness).

## Why

Every defect hunt this week began with the same question — *which firmware is
this device actually running?* — answered by behavioural fingerprinting (the
28th: gap arithmetic; the 29th: watching whether panel values moved). Schema 3
turns that into a field. It also closes the loop the flash-handoff md5 only
half-closes: the md5 identifies the *image*, `fw_commit` identifies the
*source state*, over the air, for as long as the device runs.

## How the hash gets in — and why it is never committed

A committed file cannot contain the hash of the commit that contains it, so
the hash exists only in the binary: `scripts/build.sh` injects
`-DFW_GIT_HASH24=0x<hash>` through `compiler.cpp.extra_flags`. Channel choice
matters: `build.extra_flags` looks equivalent but is owned by the board
definition (`__SAMD21G18A__`, USB flags) — overriding it produces a broken
build. `compiler.cpp.extra_flags` is empty at platform level and reaches every
translation unit we have (the sketch is all C++).

The script is the enforcement point the operator asked for:

1. host suite + decoder suite must pass;
2. release mode **refuses a dirty tree** (lists the dirt) — the guarantee
   "the hash names a commit containing exactly this source" is achieved by
   refusing, not by auto-committing: commit messages carry meaning in this
   repo, and a script-generated "wip" commit would debase them;
3. `--dev` permits iteration builds on a dirty tree, stamped 0x000000 →
   `fw_commit: null`, so an ad-hoc image self-identifies as unofficial;
4. `--clean` prevents a cached object from masking a flag change;
5. output lands in `build/` (gitignored) plus a release copy named
   `gisebo-05-fw-<hash>.ino.bin`.

## Verification

End-to-end on the real path: `scripts/build.sh` at `b4f1fa7` ran both suites,
embedded `0xb4f1fa`, produced `gisebo-05-fw-b4f1fa.ino.bin` (75180 B, md5
`1dc79d57…`); dev mode on the pre-commit dirty tree correctly stamped
0x000000. Host suite +2 cases (hash bytes big-endian, zero sentinel); decoder
59/59 (+6: round-trip, null, leading-zero preservation, wrong-length,
schema-1/2 compat). Formatter live and byte-identical; the on-air device still
emits schema 2, which the suite keeps as its compat vectors.

Flash-verify when the image next goes on: DEV serial prints
`Firmware commit: B4F1FA` at boot, and the first verbose frame reports
`fw_commit: "b4f1fa"` (or whatever HEAD is by then — build fresh rather than
flashing this artifact if more lands first).
