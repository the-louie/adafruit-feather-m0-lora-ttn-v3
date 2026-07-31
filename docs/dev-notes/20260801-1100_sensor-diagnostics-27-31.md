# Items 27-31: the sensor-diagnostics batch

Date: 2026-08-01 11:00 CEST
Implements all five follow-ups from the gisebo-01 failure review in one build.
Firmware pending flash (gisebo-05 stays on its verified image until the
operator chooses); decoder half is live on TTN, byte-identical.

## 27 -- water-step plausibility (`sensor_plausibility.h`, fault 0x0100)

The one failure class the diagnostics could not see: a reading that is valid,
plausible and wrong (sensor out of the water, measuring air). The check flags a
step between consecutive wakes that exceeds what water's thermal mass permits.

Thresholds are derived from fleet data, not guessed -- measured this session
from the 2026-07-16 production capture:

    gisebo-01 tank (submerged, 30-min):  p99 0.93, max 0.93 degC/h
    gisebo-04 fridge (compressor):        p99 1.79, max 1.79 degC/h
    gisebo-05 bench in direct sun (air):  max 7.4 degC/h

`TEMP_MAX_RATE_C_PER_H = 10` sits 5.6x above the worst legitimate water rate
ever recorded by this fleet and above the bench-in-sun maximum, so the test
unit does not cry wolf; `TEMP_STEP_FLOOR_C = 1.0` absorbs the 0.2 degC wire
quantum at short intervals. The rate scales with the interval (15 degC across a
6 h winter wake passes; the same step across 30 min fails). Deliberately a
detector for the ACUTE cases; stated non-coverage: a sensor frozen INTO ice
pins flat near 0 and needs seasonal reasoning (backend, item 10). No verdict
without evidence: NaN, dt 0 (first sample after boot) and invalid-gap
boundaries all pass.

## 28 -- status code (fault frame schema 2, byte 11)

`ds18ReadValid` (one bit) became `Ds18Status`:
OK / NOT_FOUND / CRC_OR_NO_RESPONSE / STUCK_85 / OUT_OF_RANGE / AMBIGUOUS,
derived by the pure `ds18DeriveStatus()`. READ_FAIL still fires as before; the
status byte says which flavour. The 85.00 match is exact (the power-on default
is exactly representable) and 84.9 correctly classifies as out-of-range
instead. CRC-vs-no-response are one code: DallasTemperature cannot separate
them and both mean "bus integrity".

## 29 -- consecutive-failure streak (byte 12, persisted)

Saturating count of consecutive failed wakes, cleared on any good read,
persisted in `.noinit` (PERSIST_VERSION 3) so the join-failure reset cannot
disguise a fault that spans reboots. Converts a binary alarm into triage:
"failed once" and "failed 400 times" now look different on the wire, which the
daily fault rate limit otherwise hides completely.

## 30 -- one retry inside the wake

A failed read triggers one more conversion cycle (mode-appropriate wait, PROD
delay / DEV runloop). Costs 750 ms only in the failure path. Also re-enumerates
the bus when the cached count is 0 -- the count was previously cached in
setup() forever, so a sensor attached after boot (the bench swap-test on a
broken unit, exactly gisebo-01's situation today) was invisible until reboot.
Now the next wake sees it.

## 31 -- ROM id (bytes 13-15)

Low 3 bytes of the sensor ROM serial, captured at boot and on re-enumeration;
0x000000 = none, decoder reports null. Enough to notice a swapped sensor;
matters more in v4.

## Wire and compatibility

Fault frame: schema byte 2, length 11 -> 16. The decoder branches on the schema
byte exactly like verbose 1/2/3; the 11-byte schema-1 frames gisebo-01 sent
this morning remain decodable and are pinned as a compat vector in the suite.
persist v3 forces a clean cold boot at the upgrade flash, which was true of
every persist change so far.

## Verification

Host suite 366 assertions (16 new plausibility cases from the measured
vectors, 9 status-derivation cases, schema-2 encode cases); decoder 69/69
including gisebo-01 compat and the temp_implausible fault name. Formatter
live and byte-identical. Compiles 75316 B (28%).

Over the air after the flash: a healthy gisebo-05 must report ds18_status "ok",
streak 0, a stable ROM id, and NO temp_implausible despite balcony sun (the
threshold clears the measured 7.4 degC/h bench maximum -- if it does fire on
the bench, the threshold analysis was wrong and the fault, not the water, is
the finding).
