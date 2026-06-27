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
2. **Load the decoder as text and substitute `FIRMWARE_VERSION` before eval.** The constant is per-device (S02-06) and the harness must exercise v5, v6 and v7 semantics against the same file — but TTN payload formatters take no configuration, so a module-level constant is the only mechanism the console supports. Regex the declaration to the version under test, then eval.

   This is deliberately grubby, and the grubbiness is the point: it tests the **actual deployed artifact, byte for byte**, with no test scaffolding compiled into the file that runs in production. Given that the repo decoder turned out not to be the live one (TODO #13), anything that lets the tested file diverge from the pasted file is disqualified. The cost is a regex coupled to the declaration's text — so keep the declaration on its own line in a fixed form, and **fail loudly if the substitution does not match** rather than silently testing the default.
3. No framework unless one is already in use — a script with a non-zero exit code is enough.
4. Prove it works by replaying two real payloads from `real-world-data__20260716.json` and asserting the decoder reproduces TTN's own `decoded_payload`. This validates the harness against production truth rather than against our assumptions.

## Done when

- [ ] One-command test run, exits non-zero on mismatch.
- [ ] `FIRMWARE_VERSION` substitution works and **fails loudly** if the declaration form changes.
- [ ] At least two vectors taken from real captured uplinks pass.
- [ ] Runner documented in CLAUDE.md.
