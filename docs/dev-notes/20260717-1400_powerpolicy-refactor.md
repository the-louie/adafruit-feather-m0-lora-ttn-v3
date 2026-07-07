# The PowerPolicy refactor (S02-10 … S02-21)

## What moved

The 484-line sketch is now 423 lines plus 462 lines of Arduino-free modules, each host-tested:

| module | what | tests |
|---|---|---|
| `season.h` | seasonal baseline + 1 °C hysteresis. **Shared** — neither policy owns it | 26 |
| `power_policy.h` | the interface, plus `voltageOffsetHyst()` | — |
| `policy_primary.h` | 6 V bands, the interval ladder | 21 |
| `payload.h` | bytes 0–8, encoders, sentinels | 28 |
| `uplink_schedule.h` | send decision + uplink counter | 19 |

**96 host assertions**, plus 10 decoder vectors. All green.

## Behaviour-identical, and proven so

The one assertion that matters: `payloadBuildCore()` reproduces **gisebo-01's real captured uplink byte-for-byte** from its inputs — `[4, 173, 7, 134, 134, 134, 134, 135, 135]`, and the short-batch case too. If that ever differs, the refactor changed the wire format and every deployed decoder is wrong.

The tests also anchor to production at the policy level: 16.8 °C / 5.768 V → index 4, which is exactly what gisebo-01's byte 0 says.

## The tests corrected me three times

Worth recording, because it is the argument for writing them first:

1. **Season boundaries.** I asserted `SUMMER → MID at 15.0`. The condition is `tempC < LEAVE`, so 15.0 **holds** — the gap is `[15.0, 16.0)`.
2. **Hysteresis dithering.** I asserted zero transitions at a band edge. The right property is **at most one**: the asymmetric rule degrades on the first noise dip, then latches. Zero would need symmetric hysteresis, which would delay the protective response.
3. **Float truncation.** I asserted `encodeWaterTemperature(16.8) == 1680`. It is **1679** — `(int)(16.8f * 100.0f)` truncates, and the original does the same. Preserved, not fixed.

Each time the code was right and my expectation was wrong. A refactor verified only by "it compiles" would have shipped all three misunderstandings as changes.

## The deliberate behaviour change

**Voltage-band hysteresis** (S02-19). Quantified over 200 wakes at 5.000 V ±19 mV:

| | offset flips |
|---|---|
| bare threshold (as shipped) | **107** — interval thrashes 4↔5 |
| 50 mV hysteresis | **1** — degrades once, then latched |

Rule: degrade at the nominal edge, improve at nominal + 50 mV. Asymmetric on purpose — never delay the protective response. Paired with averaging 16 ADC samples, which attacks the noise at source rather than only masking it.

This makes `voltage_offset` **latched state**, so it must go in `.noinit` beside the season state (sprint 03).

## The guard that nearly became the bug

S02-21's device-count check first **blinked forever** on `count != 1`. That would have turned a dead sensor — the common case — into total silence: no temperature *and* no battery, unrecoverable without a site visit. A self-inflicted silent decommission, in a project whose entire backlog is silent decommissions.

Now: `count == 0` keeps running (−127 already encodes as a null); `count > 1` reports nulls but keeps uplinking. **Reporting nothing is recoverable. Reporting the wrong sensor's water as the surface is not.**

## Flash

| | bytes | |
|---|---|---|
| S01-00 baseline | 61632 | |
| after `idle(750)` fix | 61588 | −44 — the RTC alarm path cost flash to be wrong |
| after schedule extraction | 61564 | −24 |
| after policy split | 61816 | +252 — vtable + latched state |
| after v4 guards | 62032 | +216 |

23% of flash. The whole refactor cost 400 bytes.

## Not verified

No hardware. Everything here is compile-verified plus host tests. Sprint 06 owns:

- the `idle(750)` fix actually waits, on a **PROD-strapped** unit (never DEV — the defect does not exist there)
- `PrimaryCellPolicy` really is behaviour-identical on silicon
- the vtable's flash and RAM cost in situ
