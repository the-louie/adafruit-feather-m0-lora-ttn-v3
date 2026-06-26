# S05-12 — Backend retroactive lag correction

**Estimate:** 2 h
**Backlog item:** TODO #1
**Depends on:** S01-16, S02-01
**Needs hardware:** no

## Context

Every PROD reading from a v6 unit is one interval late, and the live decoder confidently timestamps each sample with a value that is wrong by exactly that much.

S01-16 decided whether to correct, caveat, or discard. This implements it.

## Steps

1. If correcting: shift each v6 series back one interval, using byte 0's recorded interval per message. Only valid where the interval was stable across the shift — detect and skip where it changed.
2. For gisebo-04 (V5, no interval byte), correction rests entirely on the assumed 5-minute cadence. If S01-06 could not establish V5's behaviour, **do not correct it — caveat it.** Guessing here would fabricate data.
3. Mark corrected data as corrected. Never silently rewrite history.

## Done when

- [ ] The S01-16 decision implemented.
- [ ] Interval changes handled or skipped, never guessed through.
- [ ] Corrected data is labelled.
