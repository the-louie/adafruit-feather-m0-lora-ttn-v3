# S01-00 — Prove the project still builds

**Estimate:** 1–2 h
**Backlog item:** — (gap found 2026-07-17; nothing else covered it)
**Depends on:** nothing
**Needs hardware:** no
**Do this before anything else in sprint 01.**

## Context

**The plan's only verification mechanism for sprints 02–04 is "it compiles" — and nobody has established that it compiles.** No task covered this. Every estimate, every "compile-only verification" exit criterion, and the entire decision to ship firmware to a post at a lake without a bench rests on a toolchain nobody has run.

The risk is not hypothetical. `CFG_eu868` must be set in **`lmic_project_config.h`, which lives inside the library, not this repo** — it is invisible, unversioned, and not reproducible from a clone. The firmware `#error`s without it. Whoever last built this may have a working environment nobody else can recreate, and the fleet's current binary may be unreproducible. That is exactly the shape of TODO #14, where `gisebo-04` runs firmware whose source cannot be found.

If the build cannot be reproduced, sprint 02 stalls on day one and the plan has no verification at all.

## Steps

1. Install `arduino-cli`; add the Adafruit SAMD board package; select **Adafruit Feather M0**.
2. Install the four libraries: MCCI LoRaWAN LMIC, ArduinoLowPower, OneWire, DallasTemperature. **Record the exact versions** — the DeviceTimeReq contract in TODO #6 was verified against MCCI LMIC v6.0.1 and an older LMIC may lack `LMIC_requestNetworkTime()` entirely.
3. Set `CFG_eu868` in the library's `lmic_project_config.h`. Confirm the `#error` fires without it — that guard is load-bearing and should be proven, not assumed.
4. **Compile the sketch unchanged.** Record flash and RAM usage: that is the baseline S02-02 compares against, and there is no baseline today.
5. **Commit a reference copy of `lmic_project_config.h`** into the repo with a note on where it must be placed. It cannot live in its real location under version control, but it must stop being folklore.
6. Write down the exact commands that worked, in CLAUDE.md. If a second person cannot follow them to a green build, this task is not done.
7. If the build **fails**: stop and escalate. Everything downstream assumes it passes, and a broken or unreproducible toolchain outranks every other item in the backlog.

## Done when

- [ ] The unmodified sketch compiles clean for Feather M0.
- [ ] Library versions recorded and pinned; MCCI LMIC confirmed as v6.0.1 or the API delta assessed.
- [ ] Flash/RAM baseline recorded.
- [ ] `lmic_project_config.h` committed as a reference copy with placement instructions.
- [ ] The commands are in CLAUDE.md and reproducible by someone else.
