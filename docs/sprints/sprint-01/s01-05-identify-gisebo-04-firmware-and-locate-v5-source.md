# S01-05 — Identify gisebo-04 firmware and locate V5 source

**Estimate:** 2 h
**Backlog item:** TODO #14
**Depends on:** none
**Needs hardware:** no

## Context

`gisebo-04` sends 8-byte payloads — the pre-interval-byte V5 protocol. **That firmware's source is not in this repo.** A unit whose source we do not have cannot be maintained, reasoned about, or trusted.

## Steps

1. Search other repos, branches, and the sibling `waveshare-rp2040-lora-ttn` project for V5 source.
2. Check this repo's git history — v6 may have been an in-place edit over V5.
3. If found: confirm the 8-byte layout (2 bytes battery+seq, 6 temps) and record the commit.
4. If not found: record that explicitly. It makes reflashing to v6 the only route back to a known state.

## Done when

- [ ] V5 source located, or its absence recorded as a finding.
- [ ] The 8-byte layout documented either way — it is recoverable from the captured data.
- [ ] Decision noted: can gisebo-04 be maintained, or must it be reflashed blind?
