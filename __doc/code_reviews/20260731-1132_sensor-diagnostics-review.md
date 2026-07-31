# Code review, sensor diagnostics and build provenance (2c93b5a..f1935b3)

Date: 2026-07-31 11:32 CEST
Scope: every line changed since the previous review report landed, which is the verbose schema 3 commit, the build script with its fixed keys mode, and the five sensor diagnostics items 27 through 31, roughly 580 diff lines across the sketch, three headers, the decoder, the build script, and three test files.
Method: three passes as specified, a pattern scan, a logic and edge case pass, and a downstream impact pass, followed by a comment quality audit.

## Summary

The queue contains the water step plausibility check with fleet derived thresholds, the DS18B20 status code and its pure derivation function, the persisted failure streak, the sensor ROM identity, the in wake retry with hot plug re enumeration, fault frame schema 2, verbose schema 3 with the compile time git hash, the release build script, and the decoder branches for all of it. The pure logic is host tested, 366 host assertions and 69 decoder tests pass, the wire formats are versioned, and old captures keep decoding. The review found no critical defect, but it found two medium findings where the new diagnostics fail to report truthfully in exactly the scenarios they were built for, two robustness gaps in the build script, and small hygiene issues in a test and two comments. All were remediated in this pass, verification below. Nothing was complex enough to defer, so no new TODO entries were needed.

## Critical findings

| Issue | Severity | Description | Suggested Fix |
| :--- | :--- | :--- | :--- |
| Stuck 85 never retried | Medium | The retry trigger was `tempC != tempC \|\| tempC <= -100.0f`, which covers the NaN and disconnect classes but excludes 85.0. A stuck 85 reading means the conversion never ran, which is precisely the transient class the retry exists to absorb, a sensor that browned out or was hot plugged mid conversion recovers completely on one fresh conversion. As written, one transient produced a fault frame instead of a clean sample. | Trigger the retry on any non OK provisional status by reusing `ds18DeriveStatus()`, the same tested function that later derives the reported status. This also retries the out of range flavour, which costs one conversion window in a rare failure path and can only improve the sample. Implemented. |
| ROM identity goes stale across a live swap | Medium | `g_ds18Rom` was captured only in `setup()` and in the count zero re enumeration. DallasTemperature's `getTempCByIndex()` performs a fresh bus search on every call, so a sensor swapped between wakes reads perfectly while bytes 13 to 15 keep reporting the old serial. Item 31 exists to notice a swapped sensor over the air, and this is exactly the scenario where it stayed silent. | Refresh the ROM after every successful read, one extra bus search of a few milliseconds against a 750 ms conversion window. On failure the last known serial is kept deliberately, so a not found frame still names which sensor was lost, now documented in `diagnostics.h`. Implemented. |
| Topology faults fossilize | Low | Re enumeration ran only when the cached count was zero. A two sensor bus reduced to one stayed AMBIGUOUS until reboot even though reads would now succeed, and a chain that failed open after boot kept the cached count of one, reporting the crc flavour forever when the truthful signature is not found, the exact signature that identified the gisebo-01 failure. | Re enumerate on any failed read, so the status byte always describes the bus as it is now rather than as `setup()` found it. The search cost lands only in the failure path. Implemented, subsumed into the same retry restructure as the first finding. |
| Build script accepts typos as release builds | Low | `scripts/build.sh --devv` or any unrecognized argument fell through to release mode, the strictest and least expected interpretation of a typo. | Validate the argument against the three known forms and exit with an error otherwise. Implemented. |
| Empty fixed keys label | Low | If the `grep` for `FIXED_DEVEUI_MSB` ever fails against a reformatted `fixed_keys.h`, `LABEL` is empty and the script ships `FIXEDKEYS--fw-<hash>.ino.bin`, an image whose whole naming contract is carrying the device identity, with the identity missing. | Fail hard when the label extraction comes back empty. Implemented. |
| Dead code in a decoder test | Low | The diag2 block in `test/run_v7.js` built a frame into `f`, decoded it into `fr`, then ignored both and rebuilt the same frame inline inside the assertion. Dead variables in a test invite the next editor to assert on the wrong object. | Assert on `fr` and delete the duplicate inline construction. Implemented. |
| Comments narrating history | Low | The `gitHash24` field comment justified itself with "this week's defect hunts", which is history, not concept, and the fault bitmap defined bit 0x0100 above bit 0x0080, so the map no longer read in ascending order. | Rephrased the comment to state the timeless purpose, reordered the defines. Implemented. |

## Findings accepted without change

These were examined and judged correct as written, recorded so the next review does not rediscover them.

1. The plausibility check is edge triggered. `g_prevTempC` adopts the implausible reading, so one excursion raises the fault once on its leading edge rather than latching against an ever staler baseline, and a sensor left in air re alerts on each new acute swing, which is the desired cadence. A sentence now states this at the call site.
2. `dt` for the step check is `sleepIntervalSeconds`, which at read time still holds the duration actually slept, because interval changes land only after `EV_TXCOMPLETE`, later in the cycle. The pairing is correct by ordering, not by luck, and the ordering is one of the sketch's stated invariants.
3. `tempC == 85.0f` compares exactly. The power on default is exactly representable in float and the library returns raw sixteenths, so no epsilon is needed, and the header says why.
4. A genuine 85.0 degree reading is classified as stuck. Accepted, water at 85 in this application is a bigger event than a false fault, and the comment records the tradeoff.
5. `persistSeal()` runs in the read path for the streak and again later for the schedule. Sealing is idempotent and cheap, and coupling the two writes would entangle functions that are otherwise independent.
6. `DIAG_PAYLOAD_V1_LEN` and `DIAG_VERBOSE_V2_LEN` are defined but unreferenced. They are the frozen record of wire history next to the current lengths, documentation with a compiler checked spelling.
7. The `PERSIST_VERSION` bump to 3 discards `.noinit` state once at the next flash. That is the versioning contract working as designed, the streak field would otherwise be read from an older layout's bytes.
8. The decoder reports `ds18_rom` alongside a not found status. With the last known semantics now documented, that pairing is informative, it names the sensor that was lost.

## Downstream impact assessment

- Fault frames on FPort 1 and 2 grew from 11 to 16 bytes and verbose frames on FPort 3 from 34 to 37, both far inside the 51 byte EU868 DR0 budget. The formatter on TTN was verified byte identical to `decoders/gisebo-05-v7.js` at upload.
- The decoder adds `ds18_status`, `sensor_fail_streak`, `ds18_rom`, and `fw_commit` to the influx stream. All are additive, existing dashboard fields are untouched, and schema 1 and 2 captures on the wire continue to decode through their own branches, covered by tests including the gisebo-01 compatibility vector.
- The retry restructure changes wake timing only in the failure path, one extra conversion window plus a bus search, and never while the radio owes the network anything, the read runs before any TX.
- The device currently in the field runs the 2026-07-29 image and is unaffected until the operator flashes, at which point the persist version bump costs one cold boot, expected and documented.

## Verification

- `test/host/run_tests.sh`, all suites pass, 366 assertions.
- `node test/run.js` and `node test/run_v7.js`, all pass, 69 decoder tests, including the reworked diag2 fault name assertion.
- `arduino-cli compile` clean as a compile check, the flash image remains the build script's output.
- `scripts/build.sh --devv` now exits 1 with a usage error, `--dev` and the bare form behave as before.
