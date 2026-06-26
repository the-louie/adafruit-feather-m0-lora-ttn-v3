# S02-15 — Extract payload assembly

**Estimate:** 2 h
**Backlog item:** TODO #4
**Depends on:** S02-12
**Needs hardware:** no

## Context

Bytes 0–8 are byte-identical across both variants and must stay that way. Extracting assembly makes the solar variant's 6-byte append a clean addition rather than a rewrite.

## Steps

1. Move payload building into `payload.h` / `payload.cpp`.
2. Keep the temperature sentinel encoding exactly: 250 null, 251 too cold, 252 too warm, else `(degC + 10) / 0.2`.
3. Keep the battery encoding: 12-bit offset from 3000 mV, clamped both ends.
4. The policy appends its own bytes after byte 8 — core does not know about solar.

## Done when

- [ ] Payload assembly in its own module.
- [ ] Bytes 0–8 byte-identical to pre-refactor.
- [ ] Sentinel and battery encoding preserved.
