# Deployment checklist: gisebo-05 to the lake (2026-08-02; originally 2026-08-01)

**Status 2026-08-02 morning: everything through section 2 is DONE.** fw
`95232b` verified on the wire, production sensor `ffcf35` healthy (status ok,
streak 0), pack charged to 4.2 V, reed-switch field reset installed and
verified (two clean POR boots). The 08-01 attempt was postponed by a faulty
sensor assembly (hard DQ-GND leak, condemned, replaced). **16:08 CEST: strap cut, PROD CONFIRMED on the wire** -- first data frame on
FPort 11 and boot fault frame on FPort 1 (ds18 ok, `ffcf35`, faults empty,
battery 4.15 V, RSSI -53). The reed field reset was also verified THROUGH the
closed case (16:04, clean POR boot burst). Remaining: mount at the lake.

Taking gisebo-05 from DEV bench duty to PROD at the lake. The order below
matters: PROD emits no verbose frame, so several things are only verifiable
while the strap is still on.

## 1. Before touching the device

- [ ] **Charge the pack to full (~4.2 V).** The voltage gate is then green from
      the first evaluation, and the pack has days of buffer while the sun EWMA
      (wiped by the persist v3 bump) re-earns the bonus -- expect the 5-min
      cadence from day-2 afternoon at the earliest.
- [ ] **Build fresh at flash time**: `scripts/build.sh` on a clean tree, AFTER
      the morning's capture commits, so the embedded fw_commit names HEAD.
      Do not reuse an older artifact.

## 2. Flash and verify IN DEV, on the bench (strap still fitted)

The boot burst (verbose + fault + data) is the only place fw_commit and the
full diagnostic state are visible -- PROD never sends FPort 3.

- [ ] Flash the fresh release image (double-tap RESET, bossac --offset=0x2000).
- [ ] Wait for join + boot burst on TTN, then check:
  - [ ] verbose (FPort 3): `fw_commit` = the new hash, `diag_schema` 3,
        `mode SOLAR`, `ina219_config_ok`, sane battery.
  - [ ] fault frame (FPort 2): `ds18_status "ok"`, `sensor_fail_streak 0`,
        faults empty.
  - [ ] **Record `ds18_rom`** -- this is the reference identity for all future
        swapped-sensor detection (item 31). Write it into the fleet notes.

## 3. Switch to PROD

- [ ] Power off. **Remove the pin-11 strap entirely** -- the pin must float
      (INPUT_PULLUP); do not tie it high. STRAP_PIN is read once in setup(),
      so the mode change needs this power cycle.
- [ ] Power on (bench or lake, wherever there is coverage) and verify on TTN:
  - [ ] Join accept, then first data frame on **FPort 11** -- the port alone
        proves PROD mode AND a correct solar probe in one observation.
  - [ ] Boot fault frame on **FPort 1**, faults empty.
  - [ ] Note RSSI/SNR once at the lake position; compare against the bench to
        know the link margin.

## 4. Physical install

- [ ] DIO1-to-pin-6 jumper intact (the pin map assumes it).
- [ ] Reed switch (EN-GND, normally-open) tested THROUGH the closed enclosure
      wall; magnet spot marked outside. A 3 s magnet hold = full power cycle
      (the field hard reset); every reset costs a rejoin + cold boot.
- [ ] Antenna connected before power-up.
- [ ] Panel: face south, up to 10 deg west of south is fine; use the FULL
      15 deg tilt of the holder (docs/panel-placement-guidance.md).
- [ ] Sensor fully submerged at its intended depth, cable strain-relieved,
      silicone entry not load-bearing.
- [ ] Enclosure sealed; USB port unused (PROD detaches USB anyway).

## 5. Expectations after deployment (do not misread these as faults)

- **No verbose heartbeat.** The hourly FPort-3 frame was DEV-only. PROD
  telemetry is: data every 6 wakes (6 h at the 60-min interval), fault frame
  once per boot, on any new fault, and at most one re-alert per day.
- **First TX after a long deep sleep may be delayed** -- LMIC's duty-cycle
  accounting is respected by design; this is legitimate, not a stall.
- **First hardware exercise of the PROD paths.** deepSleep, USB detach, the
  join-failure reset and the PROD conversion wait have never run on this
  hardware -- that asymmetry is exactly why the fault frame exists. The
  FPort-1 boot frame is the health check for it.
- **Cold-start season settling**: two uplink cycles to reach the true season
  from the Summer default if the water disagrees.

## 6. Backend (open, tracked as item 10 -- not blocking deployment)

The application webhook already feeds gisebo-05's v7 uplinks into
telegraf -> influx; TTN storage also captures everything. But the grafana
dashboards still speak gisebo-01's old schema, so the site stays dark in
dashboards until the backend migrates to the v7 fields. The recipient
discards silently -- remember the cutover trap note in the fleet memory.

## 7. Flash-verify list closed by this deployment

Items 27-31 plus the 2026-07-31 review fixes ride this flash. Criteria (from
TODO.md): ds18_status ok / streak 0 / stable ROM / no temp_implausible across
a full day-night swing / fw_commit present in the DEV boot burst.
