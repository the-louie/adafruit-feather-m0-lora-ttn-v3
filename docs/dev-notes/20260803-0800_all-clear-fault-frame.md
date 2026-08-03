# Item 32: the all-clear fault frame

Date: 2026-08-03 08:00

## Summary

`diagShouldSend`/`diagMarkSent` (diagnostics.h) now produce exactly one fault
frame with `fault_bits: 0` when a reported fault set empties. Before this, a
fault appearing was announced but a fault clearing never was -- the backend's
newest fault frame stood indefinitely (live case: the deployment splash's
`temp_implausible` showed as the current state on the NOC dashboard for hours
after it self-cleared, because nothing ever contradicted it).

## Design

A marker bit (`DIAG_CLEAR_SENT`, 0x8000 -- never a wire bit) lives in the HIGH
bit of the existing persisted `diagLastSentFaults` latch, so **no persist
version bump**. Semantics:

- fault set empties, bits latched, marker clear -> send one clear frame,
  set the marker, KEEP the latched bits.
- Keeping the bits is the flap defence: a fault returning after its clear
  frame is a KNOWN bit, so it rides the once-per-day re-alert path instead of
  being "new" and prompt -- a flapping fault costs at most two frames per day
  (one alert, one clear), not two per flap.
- Any frame carrying nonzero faults drops the marker (edge sends explicitly,
  re-alerts by re-baselining).
- Clockless-safe: the all-clear needs no epoch (episodic by construction).

Decoder, telegraf and dashboards need nothing: `fault_bits: 0` already decodes
as `faults: [], healthy: true`, and the NOC "Halsa" ribbon flips back to FRISK
on its own once this firmware is flashed.

## Verification

- Host: new section 5b (episode, one-per-episode, flap bounds, distinct-fault
  promptness, marker lifecycle) + section 5's clears->no-send assertion
  deliberately INVERTED to clears->one-send. All suites pass.
- Compile clean. On-device verify (next flash): unplug the sensor for one
  wake, replug; expect the alert frame, then one `fault_bits: 0` frame after
  the recovering wake.
