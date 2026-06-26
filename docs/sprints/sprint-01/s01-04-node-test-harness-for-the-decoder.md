# S01-04 — Node test harness for the decoder

**Estimate:** 2 h
**Backlog item:** TODO #3
**Depends on:** S01-03
**Needs hardware:** no

## Context

There is no executable check on the firmware↔decoder contract. That absence is the direct cause of both confirmed defects surviving: `doc/test-payloads.md` asserts behaviour (`sequence 0` on the fast-flush) the code has never had, and nobody noticed because nothing ran it.

This harness is the thing that stops the next one.

## Steps

1. Minimal Node runner: load the canonical decoder, feed `{bytes, fPort}`, deep-compare against expected JSON.
2. No framework unless one is already in use — a script with a non-zero exit code is enough.
3. Prove it works by replaying two real payloads from `real-world-data__20260716.json` and asserting the decoder reproduces TTN's own `decoded_payload`. This validates the harness against production truth rather than against our assumptions.

## Done when

- [ ] One-command test run, exits non-zero on mismatch.
- [ ] At least two vectors taken from real captured uplinks pass.
- [ ] Runner documented in CLAUDE.md.
