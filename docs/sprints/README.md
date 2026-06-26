# Sprints

Two-week sprints, one developer, ~30 h effective capacity each. Tasks are 1–2 h; anything larger is split.

Backlog items live in `TODO.md` at the repo root. Design decisions behind the solar work live in `docs/solar-variant-design.md`.

| Sprint | Dates | Theme | Tasks | Status |
|---|---|---|---|---|
| [01](sprint-01/) | 2026-07-16 → 07-29 | Establish ground truth | 18 | Ready |
| [02](sprint-02/) | 2026-07-30 → 08-12 | Fix confirmed bugs, split the core | 20 | Ready |
| [03](sprint-03/) | 2026-08-13 → 08-26 | Probe, persistent state, timekeeping | 15 | Ready |
| [04](sprint-04/) | 2026-08-27 → 09-09 | Solar policy | 19 | Ready |
| [05](sprint-05/) | 2026-09-10 → 09-23 | Vectors, backend, documentation | 20 | Ready |
| [06](sprint-06/) | 2026-09-24 → 10-07 | Core on-device verification | 12 | 🟡 Gated on delivery |
| [07](sprint-07/) | 2026-10-08 → 10-21 | Solar bring-up and field readiness | 12 | 🟡 Gated on delivery |

Sprint 06 and 07 dates are **provisional**. Hardware ETA is "later than sprint 03" and not firm.

## Working agreement

- **Commit in small batches**: one function + its test (if available) + its documentation, per commit. Several commits per task; never one per task or per sprint.
- Every non-trivial change gets a dated dev-note in `docs/dev-notes/` (`YYYYMMDD-HHMM_slug.md`): summary, rationale, verification.

## The constraint that shapes this plan

**Hardware arrives later than sprint 03**, so verification cannot be folded into the sprints that make the changes. Sprints 02–05 are verified by compilation and host-side tests only; sprints 06–07 verify retroactively.

The cost of that, stated plainly rather than buried:

- **Sprint 04 picks the index-2 floor on an unmeasured number.** The per-wake energy estimates disagree by 35× (~0.075 mAh from the brief's own figures vs ~0.002 mAh from a model of the wake). S06-05 measures it and S06-09 revisits the decision — after it was made.
- **Sprint 03 ships the RTC ownership seam unverified.** `ArduinoLowPower` and `RTCZero` both own the RTC; whether double-`begin()` collides is unknowable without hardware (S03-10 documents it; S06-03 finds out).
- Both sprint 06 and 07 carry a 4 h **remediation buffer**. That is not padding — sprints 02–04 reach a bench having been verified by a compiler, and the realistic expectation is that verification finds something.

Sprint 01 task 15 argued for a bench unit on the procurement order. It is now ordered — Feather M0, DS18B20, INA219, panel, 18650 pack.

**No MPPT — decided 2026-07-17.** No external charger; the panel feeds the USB pin through a Schottky and the INA219, charging via the onboard MCP73831, exactly as originally designed. It will motorboat and cost perhaps 30–50% of harvest, which is affordable against ~10× summer surplus — it was never an energy decision. The signal risk is covered by the healthy-battery gate (motorboating only occurs while charging; the solar bonus only applies to a near-full pack that has terminated the charger, so the two never overlap), and the measurement risk is mitigated by the INA219's own 128-sample averaging. S01-12 records the rationale; **S07-05 is the one measurement that could reopen it**, and there is no hardware fallback by decision.

Supercap and over-discharge protection are still outstanding — they depend on the sprint 01 spikes. Second order lands in S07-08.

## Two constraints that survive having hardware

**The `idle(750)` bug is PROD-only.** It cannot be verified on a DEV-strapped unit — the DEV path always used an `os_runloop_once()` loop and was never affected. Verification needs a strapped PROD unit with USB detached: no serial, debugging over LoRa. That asymmetry is exactly how the bug survived for months.

**Solar cannot be observed in DEV.** USB puts 5 V on the pin the panel feeds, so the Schottky blocks the panel and the INA219 reads ~0 mA on a 5 V bus. Serial logs and real solar are mutually exclusive by construction — bring-up runs on a bench PSU with telemetry over the air on FPort 21.

## What the 2026-07-16 data changed

The TTN capture (`docs/dev-notes/real-world-data__20260716.json`, 107 uplinks, 2 devices) arrived mid-planning and invalidated several assumptions. Sprint 01 exists largely because of it:

- **The fast-flush fires every 16 wakes**, not once per join — `wakeCounter` is 4-bit and wraps. Confirmed exactly: sequence takes only {1, 7, 13}, with a 4-sample batch at seq 1. A third of all uplinks ever sent carry a short batch and two dead bytes.
- **`rebootDetected` has never fired.** Sequence is never 0 across 107 uplinks, on either device.
- **`LowPower.idle(750)` does not hang** — both units transmit — so it returns early. The library truncates `750/1000` to a zero-second alarm, so every PROD temperature reading is lagged one wake interval.
- **The deployed decoder is not `ttn-decoder-v6.js`.** It is length-aware, extrapolates per-sample timestamps, and reports `version: 5`. The repo copy is a stale artifact nobody runs.
- **The fleet runs two protocols.** `gisebo-01` on 9-byte v6, `gisebo-04` on 8-byte V5 whose firmware source is not in this repo.

The interval algorithm itself is confirmed **working**: gisebo-01 sits at index 4 = Summer base (16.8 °C water) + healthy battery (5.768 V), exactly as designed.

## Dependency spine

Sprint 01 is deliberately all desk work — it needs no hardware and unblocks everything else.

```
01 ground truth ──► 02 fixes + refactor ──► 03 probe/state/time ──► 04 solar policy ──► 05 vectors/backend/docs
   │                                                                     ▲
   │                                                                     │ floor decision made blind,
   │                                                                     │ revisited in S06-09
   │
   └── spikes 13-14 ──► S07-08 second order (supercap, protection)
                                    │
   hardware delivery ──► 06 core verification ──► 07 solar bring-up ──► field
```

The first board to receive new firmware is a **production board on a post at a lake** — there are two units in the field and the bench units have not arrived. S05-16 and S07-12 both treat rollback as a real question, because at that site the answer may be a site visit.
