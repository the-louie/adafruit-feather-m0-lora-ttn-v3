# S05-01 — Move test-payloads into docs and update references

**Estimate:** 1 h
**Backlog item:** TODO #3
**Depends on:** sprint-04
**Needs hardware:** no

## Context

`doc/test-payloads.md` is the only file in `doc/`; everything else lives in `docs/`. It also documents a protocol two revisions stale.

## Steps

1. `git mv doc/test-payloads.md docs/test-payloads.md`; remove the empty `doc/`.
2. Update references in `TODO.md`, `CLAUDE.md`, `docs/solar-variant-design.md`.
3. Delete the V5 vectors wholesale — they fail the length check and validate nothing. Do not migrate them.
4. Drop the trailing question to the user at the end of the file; it is conversational residue from when the file was generated.

## Done when

- [ ] File moved, `doc/` gone.
- [ ] No dangling references.
- [ ] Stale vectors deleted rather than migrated.
