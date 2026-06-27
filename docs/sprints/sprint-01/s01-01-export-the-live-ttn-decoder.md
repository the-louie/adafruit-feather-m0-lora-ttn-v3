# S01-01 — Export the live TTN decoder

**Estimate:** 1-2 h
**Backlog item:** TODO #13
**Depends on:** none
**Needs hardware:** no

## Context

The decoder running in TTN is not `ttn-decoder-v6.js`. Evidence from the 2026-07-16 capture: it reports `"version": 5`, emits `entries` with extrapolated per-sample timestamps, omits null slots rather than emitting `null`, and correctly decodes both 8- and 9-byte payloads by branching on length. The repo file does none of these and rejects anything under 9 bytes (`ttn-decoder-v6.js:15`).

Until the real decoder is in hand, every downstream decoder task is guesswork.

## Blocked on credentials — 2026-07-17

**The `TTN_TOKEN` in `.env` cannot do this.** It authenticates fine, but `/auth_info` shows it is scoped to `telamon-temperature` with exactly **one** right:

```
name  : message-storage-1784234326066
rights: RIGHT_APPLICATION_TRAFFIC_READ
```

It is a storage-integration key — it reads stored uplinks (which is how `real-world-data__20260716.json` was produced) and nothing else. Fetching a payload formatter needs application *settings* rights: `GET /as/applications/telamon-temperature/link` returns **403 `insufficient_application_rights`**.

So this task needs either a console session or an API key with `RIGHT_APPLICATION_SETTINGS_BASIC`. **Do not widen the storage key** — a traffic-read key with no expiry is the right shape for what it does; mint a separate one, scoped and expiring, if the API route is preferred.

This is the tightest bottleneck in sprint 01: S01-02, S01-03, S01-04 and every test vector queue behind it.

## Steps

1. TTN console → application `telamon-temperature` → Payload formatters. (Or a settings-scoped API key; see above.)
2. Copy the live uplink formatter verbatim. Do not tidy it on the way through.
3. Commit as `ttn-decoder-live-20260716.js` alongside the existing file — do not overwrite yet; task 03 decides which survives.
4. Note which slot it came from (application-level vs per-device). If `gisebo-01` and `gisebo-04` have per-device formatters, capture both — they send different protocols and may well have different decoders.

## Done when

- [ ] Live formatter(s) committed verbatim.
- [ ] Source slot documented (application-wide or per-device).
- [ ] Confirmed whether the two devices share one formatter.
