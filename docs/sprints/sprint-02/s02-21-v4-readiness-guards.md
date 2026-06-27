# S02-21 — v4 readiness guards

**Estimate:** 2 h
**Backlog item:** — (from `docs/multi-sensor-v4-analysis.md`)
**Depends on:** S02-13, S02-15
**Needs hardware:** no

## Context

v4+ will add up to three more DS18B20 sensors — box, air, depth. Nothing about that is scheduled and **this task implements none of it**. These are four v3-side guards that stop v3 making v4 expensive, and one of them has a real failure mode today.

Full reasoning in `docs/multi-sensor-v4-analysis.md`. The short version: v4 will use **pin-per-role** (the pin is the sensor's identity, so no per-unit ROM table and the one-binary property survives). Given that, v3's `getTempCByIndex(0)` stays valid — provided the A2 bus really does have exactly one device.

## Steps

1. **Assert exactly one device on the A2 bus.** `sensors.getDeviceCount() == 1` after `sensors.begin()`. This is the one guard with teeth: `getTempCByIndex(0)` returns devices in **ROM-address sort order**, so the day someone wires a second sensor to A2 to try it out, the reading silently becomes a different sensor with no error and no symptom. Fail loudly instead — log in DEV, and consider a payload flag or simply refusing to report a temperature at all rather than reporting the wrong one.
2. **Rename `encodeTemperature()` → `encodeWaterTemperature()`.** The −10…+30 °C range is water-specific and the name hides it. Air at this site reaches −25 °C and would sentinel-clip all winter; a sealed box behind a south-facing panel exceeds +30 °C in July. The rename makes `encodeAirTemperature()` a natural addition and stops anyone reusing the wrong range by accident.
3. **Rename `lastTempC` → `surfaceTempC`.** Not `waterTempC` — v4 has *two* water sensors and only the 0.5–1 m one drives the season. When four temperatures exist "the temperature" stops meaning anything, and the season machine must never be fed air, box, or depth.
4. **Confirm the conversion window stays owned by the core**, not the sensor read. S02-01's `convStart`/elapsed pattern already does this and generalises directly to "issue convert on all buses, wait once, read all". Just make sure the 750 ms wait does not migrate back inside a per-sensor function while the refactor is in flight.

## Done when

- [ ] A second device on A2 is detected and reported, not silently swapped in.
- [ ] `encodeWaterTemperature()` and `surfaceTempC` throughout; no `encodeTemperature`/`lastTempC` left.
- [ ] The conversion window is still measured from `requestTemperatures()` in the core.
- [ ] No v4 functionality added. This task is guards only.
