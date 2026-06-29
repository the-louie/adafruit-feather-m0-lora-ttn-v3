# Fast-flush and uplink counter fixed (S02-04, S02-05, S02-08, S02-09)

## Change

The send decision and the 4-bit payload field now live in `uplink_schedule.h` — pure logic, no Arduino dependencies, so the host tests exercise the **same code the firmware runs** rather than a copy that drifts. Both defects this replaces survived precisely because nothing executable ever checked them.

| | before | after |
|---|---|---|
| send trigger | `wakeCounter == 1 \|\| ramCount >= batchTarget` | `firstUplinkAfterJoin \|\| ramCount >= batchTarget` |
| 4-bit field | wake counter, incremented every wake | **uplink counter**, incremented on `EV_TXCOMPLETE` only |
| reboot detection | `sequence === 0` — never fired | **`f_cnt` reset in TTN metadata** — free, and always worked |

Wire layout is **unchanged** (still the low nibble of byte 2). Semantics changed, which is exactly why this is protocol v7 and why gisebo-05 gets its own decoder rather than reinterpreting gisebo-01's bytes.

## The old model reproduces production exactly

The regression test simulates the **old** behaviour as well as the new, so the defect is pinned in executable form. Over 640 wakes:

| simulated (640 wakes) | real (139 uplinks, both devices) |
|---|---|
| `seq=1 samples=4` ×39 | `seq=1 samples=4` ×47 |
| `seq=7 samples=6` ×40 | `seq=7 samples=6` ×47 |
| `seq=13 samples=6` ×40 | `seq=13 samples=6` ×45 |

Same three states, same sample counts, same 1:1:1 ratio.

The simulation also shows the one thing production could not: **`seq=1 samples=1` fires exactly once, at wake 1** — the genuine post-boot flush. That is the only time in the device's life that `wakeCounter == 1` meant what it was written to mean. The other 39 were the counter wrapping.

The diagnosis is now confirmed three independent ways:

1. **Sequence values** — only {1, 7, 13} across 139 uplinks.
2. **Inter-uplink timing** — gaps only ever 4× or 6× the interval, visible without reading a single payload byte.
3. **This behavioural model** — predicts the distribution exactly.

## What the tests cover that production could not

- The flush is a **one-shot** and never re-arms (19 assertions, 40 wakes, exactly one partial batch).
- Wraparound 15→0 is neither a reboot nor a gap.
- **A timed-out TX advances nothing.** `ramCount` is preserved and the retry carries the **same counter value** — which is how the backend tells a retry from a fresh uplink. `ramCount` caps at `batchTarget` rather than overflowing.
- No flush before `EV_JOINED`. A device that never joins does not transmit into the void.

## Verification

- Host: 19/19 (`test/host/run_tests.sh`).
- Decoder: 10/10 (`npm test`) — unchanged, since the wire layout did not move.
- Compiles: **61564 B**, down from 61588 (S02-01) and 61632 (baseline). Both fixes made the firmware smaller.

No hardware. On-device verification is sprint 06.

## Note for the decoder

`gisebo-01`'s live decoder reads this field as `sequence` and is **frozen** — it is production and its output is the influx schema. gisebo-05's decoder (S02-06) reads the same bits as `uplink_counter`. Nothing reinterprets old bytes because nothing is reflashed: gisebo-01 keeps v6 to its retirement.

The `{1, 7, 13}` signature remains a useful fleet marker — any unit emitting it is pre-fix.
