# S04-20 — Host tests day length

**Estimate:** 2 h
**Backlog item:** TODO #7
**Depends on:** S03-15
**Needs hardware:** no

## Context

Astronomical code is easy to get subtly wrong and easy to test exactly — reference values are freely available.

## Steps

1. Test solstices and equinoxes at the deployment latitude (~57.8°N, from the gateway location in the TTN capture).
2. Test that the ratio behaves: full daylight fraction in June, short in December.
3. Test latitude sensitivity — a degree or two should be minutes, confirming the hardcoded constant is safe.

## Done when

- [ ] Solstice and equinox values correct.
- [ ] Latitude insensitivity confirmed, justifying the constant.
