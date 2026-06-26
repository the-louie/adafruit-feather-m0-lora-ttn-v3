# S06-13 — Capture a real reboot and confirm the 252 signature

**Estimate:** 1 h
**Backlog item:** TODO #1
**Depends on:** S06-02
**Needs hardware:** YES

## Context

The one check that was impossible from field data: the 2026-07-16 capture contained **no reboots**, so the 85 °C → 252 signature never had a chance to appear.

On the bench, reboots are free. This closes the loop on the original diagnosis.

## Steps

1. Flash **pre-fix** firmware, cold boot, and confirm the first uplink shows 252 in slot 0. That is the smoking gun the field data could not provide.
2. Flash post-fix firmware, cold boot, confirm a real temperature instead.
3. Confirm the S01-08 backend alarm fires on the pre-fix case and stays quiet after.

## Done when

- [ ] Pre-fix 252 signature reproduced on the bench.
- [ ] Post-fix reading is real.
- [ ] The S01-08 alarm proven to work.
