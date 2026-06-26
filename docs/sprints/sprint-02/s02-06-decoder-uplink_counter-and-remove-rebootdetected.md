# S02-06 — Decoder: uplink_counter, remove rebootDetected, bump to v7

**Estimate:** 2 h
**Backlog item:** TODO #2, #9
**Depends on:** S02-05, S01-03
**Needs hardware:** no

## Context

`rebootDetected` is confirmed dead: sequence is never 0 across 107 production uplinks. Leaving the field in ships a value that lies.

**This is a protocol version bump — v6 → v7.** Changing the 4 bits from a wake counter to an uplink counter leaves the layout **byte-identical**: same nine bytes, same positions, different meaning. **Nothing in the payload distinguishes v6 from v7.** A decoder reading a v6 wake counter as a v7 uplink counter sees jumps of 6 and reports dropped messages that never happened.

That is not a transition annoyance to wait out — it is a silent semantic change on the wire, and it is why the version exists. See `docs/solar-variant-design.md` § Protocol versioning.

Note this touches the **live** decoder promoted in S01-03, and the fleet is not reflashed yet (S01-07 sequences it).

## Steps

1. Add the per-device constant at the top of the decoder — the only thing not derivable from the payload:
   ```js
   // Set per device to match the firmware flashed on THIS unit.
   // 5 = 8-byte legacy | 6 = 9-byte wake-counter | 7 = 9-byte/15-byte uplink-counter
   const FIRMWARE_VERSION = 7;
   ```
   Decoders are set per device in TTN, and with two units where a reflash is a site visit, provisioning knows what is flashed. Use it **only** for the counter semantics.
2. **Derive the `version` output field** from length and the constant. The live decoder currently hardcodes `version: 5` and reports it for `gisebo-01`'s 9-byte v6 payloads — a static string, not a derived value. It caused a false diagnosis during planning; fix it rather than working around it.
3. Rename `sequence` → `uplink_counter` when `FIRMWARE_VERSION >= 7`, so changed semantics are visible rather than silently reinterpreted. At 6, keep reporting it as a wake counter.
4. Remove `rebootDetected` for FPorts 10/20 at any version — it has never worked at any version.
5. Add vectors to the S01-04 harness for both semantics.

## Done when

- [ ] `FIRMWARE_VERSION` constant present and documented for per-device use.
- [ ] `version` is derived, not hardcoded.
- [ ] `uplink_counter` reported at v7; wake-counter semantics preserved at v6.
- [ ] No `rebootDetected` on 9-byte payloads at any version.
- [ ] Harness covers v5, v6 and v7.
