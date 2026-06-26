# S02-07 — Host test harness for firmware logic

**Estimate:** 2 h
**Backlog item:** TODO #4
**Depends on:** none
**Needs hardware:** no

## Context

With no hardware, host-side tests are the **only** executable verification of firmware logic that exists. This harness is the single highest-leverage task in the sprint — everything downstream depends on it.

It only works if the logic is decoupled from Arduino APIs, which is what the `PowerPolicy` split enables.

## Steps

1. Compile the pure logic (season machine, interval ladder, payload encoding) for the host with a stub Arduino layer.
2. Keep the stub minimal — `millis()`, `analogRead()`, and enough to link. Do not simulate LMIC.
3. Plain assertions and a non-zero exit code are enough.
4. Wire into the same one-command test run as the decoder harness so there is one way to run everything.

## Done when

- [ ] Policy logic compiles and runs on the host.
- [ ] One command runs both firmware and decoder tests.
- [ ] Documented in CLAUDE.md.
