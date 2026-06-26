# S03-03 — Host tests probe and policy selection

**Estimate:** 1-2 h
**Backlog item:** TODO #4
**Depends on:** S03-01, S03-02
**Needs hardware:** no

## Context

The probe's failure path matters more than its success path, and it is the one nobody will exercise by hand — especially with no hardware.

## Steps

1. Stub the I2C layer; test present, absent, and garbage-response cases.
2. Test that 'absent + low VBAT' triggers whatever S03-02 decided.
3. Test that the selected policy drives the right FPort (10/20 vs 11/21).

## Done when

- [ ] All three probe outcomes covered.
- [ ] The misdetect case is pinned by a test.
