# Backend monitoring — alarm specifications

**Status: specifications, not deployed.** These need influx/grafana access the firmware work does not have. Each is written to be dropped in directly. Uplinks are the only instrument this fleet has, so every silent failure mode below needs a backend watcher — the firmware cannot alert on its own being wrong.

The telegraf schema is `telegraf-home-sync` → `telegraf.example.com/telegraf`, influx org `myhouse`, bucket `home_assistant`, tagged by `device_id`.

---

## A1 — Probe misdetect (highest value) — S01-09

**Failure:** a solar board whose INA219 was not detected boots the primary policy, whose 5.0/4.3/3.5 V bands sit above a full li-ion's 4.2 V, so it scores offset 3 and parks at a 7-day interval **permanently and silently**.

**Rule:** `device_id` in the solar set AND `f_port` in (10, 20) → alarm, high severity.

```
SOLAR_DEVICES = {gisebo-05}   # extend as solar units are built
alarm if: device_id ∈ SOLAR_DEVICES AND f_port ∈ {10, 20}
```

**Backstop** (catches a solar unit nobody added to the set): `f_port ∈ {10,20} AND battery_v < 4.5`. No healthy 6 V primary pack sits below 4.5 V and no li-ion reaches it, so the band is empty and this cannot false-fire on gisebo-01 (5.768) or gisebo-04 (5.233).

**Build it before gisebo-05 joins** — it never fires until there is something to fire about, and it is armed the instant a transit-loosened INA219 appears.

---

## A2 — idle(750) regression / stuck sensor — S01-08

**Failure:** a temperature slot reads 252 ("too warm"), which for a lake means the DS18B20 returned its 85 °C power-on default — the `idle(750)` lag signature, or a failed sensor.

**Rule:** any temperature slot == "too warm" on any device → alarm.

**Also alarm on f_cnt reset** (a reboot). The 2026-07-17 window contained none, so the lag signature has never been observed; this is the only way to catch it in the field, and gisebo-01 keeps pre-fix firmware until it retires. **Time-critical: TTN retains ~3 days, so the alarm must exist before gisebo-01's next natural reboot** or the pre-fix baseline is lost.

---

## A3 — clock never acquired — S05-09

**Failure:** a solar unit whose `DeviceTimeReq` never landed runs on the raw EWMA with no clarity normalisation — silently, possibly forever in poor coverage.

**Rule:** `clock_valid == false` for N consecutive uplinks after join (N ~ 20). Severity low — the season machine is unaffected, so it is not urgent, only invisible.

---

## A4 — pack health trend (the replacement signal) — S05-10

**Failure:** none, directly — this is how you decide *when to replace cells* on the 5–10 year consumable cycle. The supercap deliberately hides TX sag, so internal resistance cannot be inferred from a transmit; the only signal is resting voltage trending down over months.

**Rule:** trend `battery_v` per device over a long window. Correlate against `harvest_mah`:

- `battery_v` falling **with** normal harvest → the pack is ageing → schedule replacement.
- `battery_v` falling **with** falling harvest → the panel or the weather, not the pack.

**Caveat (from S07-05):** with no MPPT the harvest accumulator carries an error bar. Do not read a noisy harvest series as a failing pack until S07-06 quantifies it.

**Caveat (from the temperature finding):** `battery_v` tracks temperature at +12.8 mV/°C on alkaline. Detrend against temperature, or a cold week reads as a dying pack.

---

## A5 — panel fouling — S05-11

**Failure:** snow, leaves, or shade on the vertical panel. A bare EWMA hides it (it just looks like a low EWMA); `clarity` exposes it.

**Rule:** `clarity` persistently below ~0.5 against a high `expected_daylight_fraction`, for more than a week (a week of genuine overcast is weather, not fouling). **Gate on a full pack** (`bonus_active` or a healthy `battery_v`): with no MPPT, motorboating pulls the bus voltage around while charging, so clarity is only trustworthy once the charger has terminated. Suppress when `clarity == null` (invalid clock).

---

## A6 — historical lag correction — S05-12

**Not an alarm — a one-time data note.** gisebo-01's entire 9-byte history is timestamped one interval (30 min) late (the `idle(750)` defect, introduced 2026-03-09 12:03). gisebo-05's will not be. Same site, same panels, a systematic 30-minute discontinuity at cutover.

**Decision (2026-07-17): annotate, do not correct.** Rewriting a real series risks corrupting it, and TTN retains only ~3 days so the bulk is already in influx. Add a grafana annotation at the cutover and a standing note across gisebo-01's span. gisebo-04 (V5) is **not** affected — V5 predates the defect (see `20260717-1230_dating-the-lag-and-clearing-v5.md`).
