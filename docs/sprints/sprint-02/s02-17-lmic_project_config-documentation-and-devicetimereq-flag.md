# S02-17 — lmic_project_config documentation and DeviceTimeReq flag

**Estimate:** 1 h
**Backlog item:** TODO #6
**Depends on:** none
**Needs hardware:** no

## Context

`CFG_eu868` must be set in the library's `lmic_project_config.h` and is not settable from the sketch — the firmware `#error`s without it. That is an invisible build-environment requirement, which is exactly how a working build becomes unreproducible.

**`LMIC_ENABLE_DeviceTimeReq` does NOT need adding** — verified against MCCI LMIC v6.0.1 (`src/lmic/config.h:175-177`), it defaults to 1. An earlier draft of this task said otherwise and was wrong. Source: [mcci-catena/arduino-lmic](https://github.com/mcci-catena/arduino-lmic).

## Steps

1. Document the required `lmic_project_config.h` contents in CLAUDE.md — chiefly `CFG_eu868`.
2. **Confirm the installed library is MCCI LMIC v6.0.1** and pin it. The DeviceTimeReq API contract in item 6 was verified against that version; an older LMIC may lack `LMIC_requestNetworkTime()` entirely.
3. Verify `LMIC_ENABLE_DeviceTimeReq` really is defaulting to 1 in the *installed* copy, rather than assuming the upstream default.
4. Consider committing a reference copy of `lmic_project_config.h` so the requirement is discoverable rather than folkloric.

## Done when

- [ ] Required config documented in CLAUDE.md.
- [ ] `LMIC_ENABLE_DeviceTimeReq` enabled and verified by compile.
