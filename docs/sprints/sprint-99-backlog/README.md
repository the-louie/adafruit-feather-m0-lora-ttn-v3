# Operator backlog — what remains, and exactly what unblocks it

**Sprints 01–05 are complete.** Firmware compiles (69560 B, 26%), 190 host assertions + 46 decoder assertions green. Everything is compile- and host-verified. Nothing below is a *failure* — each item needs an access, a decision, or a physical object the autonomous run did not have.

## Needs backend access (influx / grafana / telegraf)

Full specs are in `docs/backend-monitoring.md` — ready to drop in.

| item | urgency |
|---|---|
| **A2** — 252-in-slot-0 + f_cnt-reset alarm | **Time-critical.** gisebo-01 keeps pre-fix firmware until it retires; its next natural reboot is the only chance to see the lag signature in the field, and TTN retains ~3 days. |
| **A1** — probe-misdetect alarm (solar device on FPort 10/20) | Build before gisebo-05 joins; never fires until then. |
| **A3–A5** — clock-never-acquired, pack-health trend, panel fouling | After gisebo-05 is live. |
| **A6** — historical lag annotation | At cutover. Annotate, do not correct. |
| **S08-01/02/03** — publish v7 schema, migrate telegraf additively, grafana panels | Migrate telegraf **before** the swap (it discards unknown fields, so a v7 unit is silently dropped until then). |

## Needs a decision or an order status

| item | what |
|---|---|
| **S01-15** | Firm ETA on the first order (Feather, DS18B20, INA219, panel, pack). Second order fully specified: **1 F 5.5 V supercap module** (S01-13) + **plain cells + one pack-level PCM** (S01-14). |
| **Create gisebo-05 in TTN** | New device. Safe to create (webhook discards unknown shapes), but was not done without being asked. Paste `decoders/gisebo-05-v7.js` as its formatter. |

## Blocked on hardware — sprint 06 (core) + sprint 07 (solar), 24 tasks

A board on a bench, plus panel + PSU for solar. Readiness review: `docs/release-v7-migration.md`.

**Highest-value hour: S06-13** — flash pre-fix firmware, cold boot, observe the 252 signature 139 production uplinks could not show.

What host tests could not verify (must be on silicon):
- idle(750) actually waits — **PROD-strapped unit, never DEV**.
- ArduinoLowPower + RTCZero coexist on the live RTC (S03-10 / S06-03).
- `.noinit` survives a real reset, and a brief power interruption does not produce a false-valid magic (S06-06 — the CRC's whole reason).
- INA219 wiring, calibration, load-side bus voltage.
- Disconnected-INA219 probe failure + the S01-09 alarm firing.
- Motorboat period vs the 68 ms averaging window (could reopen no-MPPT).
- Li-ion battery-temperature coefficient vs the 200 mV solar bands (S07-01).
- Sleep current vs ~290 µA; per-wake charge (settles the 35× floor disagreement).

## Blocked on a site visit — sprint 08, 12 tasks

The cutover: gisebo-05 replaces gisebo-01 at a post on a lake. Rehearsable from a bench (S08-04); executable only on site. gisebo-01 comes home intact as the rollback.

## Open questions for the operator

1. **Inrush** — a 0.5 F supercap across a fresh 1S pack is a short until charged. Series resistor (compromises the low-ESR path) or a documented connect procedure? (S01-13)
2. **Push access** — 66 commits sit on one disk with no remote reachable from here. `git push -u origin main` needs your key.
3. **Live TTN tokens** in `.env` (full-access, expires 2026-08-31) — revoke when done.
