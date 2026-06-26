# S04-14 — Decoder: accept 15-byte v7 on FPorts 11 and 21

**Estimate:** 2 h
**Backlog item:** TODO #9
**Depends on:** S01-03, S04-11, S02-06
**Needs hardware:** no

## Context

**v7 covers both new shapes**: the 9-byte primary with uplink-counter semantics (S02-06) and the 15-byte solar payload. Bytes 0–8 are byte-identical between them; FPort and length distinguish which. See `docs/solar-variant-design.md` § Protocol versioning.

**One combined decoder, not one per variant.** The live decoder already branches on length for 8 vs 9 bytes, so 15-byte extends an existing pattern rather than inventing one, and bytes 0–8 parsing stays in one place. Two files would drift — exactly how `ttn-decoder-v6.js` drifted from what actually runs.

## Steps

1. Add the 15-byte branch on FPorts 11/21.
2. Keep 8- and 9-byte handling untouched — `gisebo-04` still sends 8, and an un-reflashed `gisebo-01` still sends v6.
3. **Reuse the shared bytes 0–8 parsing rather than duplicating it.** If the solar branch needs its own copy, the structure is wrong.
4. Ensure the derived `version` reports 7 for 15-byte payloads, and that `FIRMWARE_VERSION` gates only the counter semantics — the solar shape is fully derivable from length and FPort.
5. Reject a 15-byte payload on FPort 10/20, and a 9-byte on 11/21 — those combinations mean something is misconfigured and should be loud rather than silently parsed.

## Done when

- [ ] 15-byte payloads decode on FPorts 11/21.
- [ ] Existing 8- and 9-byte paths unchanged.
- [ ] Bytes 0–8 parsing shared, not duplicated.
- [ ] Mismatched FPort/length combinations error rather than parse.
