# `clarity` no longer depends on the device clock (decoder-only)

Date: 2026-07-28 19:10 CEST
Closes TODO item **24a**. Leaves **24b** open. Decoder-only — no firmware
change, no re-flash.

## Defect

`decoders/gisebo-05-v7.js` computed clarity only when the *device's* clock was
valid:

```js
if (data.clock_valid && input.recvTime) { ... } else { data.clarity = null; }
```

Neither operand touches the device RTC:

- `expectedDaylightFraction()` takes **`input.recvTime`** — the network server's
  own receive timestamp, which The Things Stack stamps on every uplink and
  passes to the payload formatter as a `Date` — plus `SITE_LATITUDE_DEG`, a
  compile-time constant.
- `sun_ewma` is computed on-device against `dt = sleepIntervalSeconds`
  (`solar_signal.h`: `alpha = 1 - exp(-dt / TAU)`), not against the RTC.

So a flag describing the device's clock was blanking a figure derived entirely
from server-side time and a constant. The cost is not cosmetic: `clarity` is the
only signal that separates "short winter day" from "snow, leaves or shade on the
panel" — a fault a bare EWMA hides completely — and it was `null` for a unit's
entire early life, and permanently on any unit whose `DeviceTimeReq` never
landed. Exactly the period a new deployment most needs it.

## Fix

Gate on `input.recvTime` alone. One condition, plus comments recording why the
device clock is irrelevant here and what is still missing.

## The test this inverts

`test/run_v7.js` carried a case named **"clock invalid: clarity is null"** — it
asserted the behaviour being removed. It was correct against the old intent and
wrong against the new one, so it was **deliberately inverted, not deleted**:
clarity must now still be computed with `clock_valid: false`, `clock_valid`
itself must still decode as `false`, and `expected_daylight_fraction` must be
present. A second case was added for the dependency that *is* real — no
`recvTime` → `clarity: null`.

## The live fixture, and why one field diverges from TTN

`test/fixtures-live.json` exists so the expected values are **TTN's own
`decoded_payload`** rather than our assumptions. The gisebo-05 `f_cnt=0` vector
was captured under the old formatter, so its recorded `clarity: null` is now
stale. Updated to `clarity: 0.006` + `expected_daylight_fraction: 0.689`, with
the divergence documented in the file's `_comment` and the formatter re-uploaded
to TTN so future captures match again. Every other field of every vector remains
TTN's verbatim output.

**That new value is itself the argument for 24b.** 0.006 is what the fix now
reports for a device that had just cold-booted: `sun_ewma` 0.004 against an
expected daylight fraction of 0.689. Read naively that says "panel 99%
obscured"; the truth is "the EWMA has taken one sample".

## What is still missing (item 24b)

Clarity is meaningless until the EWMA has converged, and nothing checks that.
gisebo-05 right now reports `clock_valid: true`, `sun_ewma` 0.157 against an
expected fraction of ~0.68 — clarity ~0.23, which reads as a 77%-obscured panel
on a device that booted four hours ago. The old gate did not catch this either
(the clock was valid), so removing it makes nothing worse; the case was always
unguarded.

A correct gate needs the EWMA's age, which the payload does not carry. That is
the same missing field as the `uptime` / cycle-count proposed for the verbose
frame, and 24b is recorded as blocked on it. Until then
`expected_daylight_fraction` is emitted alongside clarity so the backend can
judge for itself.

## Verification

Decoder suite: **39 passed, 0 failed** (was 36 — two new assertions in the
inverted case, one new case for the `recvTime` dependency). Host suite green.
Formatter re-uploaded to TTN so the console and `decoders/gisebo-05-v7.js` stay
byte-identical — the property `test/harness.js` exists to protect by loading the
file as text.
