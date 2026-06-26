# S01-03 — Reconcile or delete the repo decoder

**Estimate:** 1 h
**Backlog item:** TODO #13
**Depends on:** S01-02
**Needs hardware:** no

## Context

Two decoders in one repo, one of them dead, is exactly how the test-vector rot happened. Pick one.

## Steps

1. Promote the live decoder to the canonical filename.
2. Delete the stale copy. Do not keep it 'for reference' — git has it.
3. Update every reference to the old filename: `TODO.md`, `docs/solar-variant-design.md`, `doc/test-payloads.md`, `CLAUDE.md`.

## Done when

- [ ] Exactly one decoder in the repo.
- [ ] No dangling references to the deleted file.
- [ ] The canonical decoder is byte-identical to what TTN runs.
