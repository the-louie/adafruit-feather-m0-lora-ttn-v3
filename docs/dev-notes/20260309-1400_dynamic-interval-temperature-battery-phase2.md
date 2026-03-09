# Dynamic interval — temperature- and battery-controlled selection (Phase 2)

## Summary

Phase 2 adds firmware logic to decide which interval index to use from the current water temperature and battery voltage. The interval is still sent in byte 0 of the 9-byte uplink and is updated only after a successful transmission.

## Algorithm objectives

- **Seasonal efficiency:** Fewer measurements in winter when tank changes are slow or less critical.
- **Battery preservation:** As the battery drains, the sensor sleeps longer to extend life.
- **Maintainability:** Simple conditional checks; no complex math; easy to test and maintain.

## Behavior

- **Seasonal baseline:** Water temperature selects a base interval with 1°C hysteresis: Summer (≥16°C) uses a short interval; Fall/Spring (8–15°C) use a medium interval; Winter (&lt;8°C) uses a long interval. Hysteresis prevents flapping at the 16°C and 8°C boundaries.
- **Battery penalty:** Healthy voltage (≥5.0 V) keeps the seasonal baseline. Lower bands add one, two, or three steps to the interval index. Critically low voltage forces a longer sleep cycle.
- **Cold-weather battery:** The algorithm uses temperature for season, so a temporarily low voltage in cold weather does not permanently lock a long interval; when temperature rises, the season can transition and the base interval can shorten again.
- **Memory safety:** The final index is clamped to the maximum table index so the device never indexes out of bounds (e.g. winter + critically low battery).
- **Single write point:** `currentIntervalIndex` and sleep duration are updated only in setup and in the block that runs after `EV_TXCOMPLETE`, so the “interval change only after successful uplink” rule is preserved.

## Documentation

- Master-plan SKILL updated to describe temperature/battery-driven interval selection, hysteresis, and backend use of byte 0 for time extrapolation.
