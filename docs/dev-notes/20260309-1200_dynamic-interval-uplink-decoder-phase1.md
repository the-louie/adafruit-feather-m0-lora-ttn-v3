# Dynamic interval in uplink and decoder (phase 1)

Phase 1 adds a single-byte interval index to the uplink payload (9 bytes total). The same fixed interval applies to all measurements in the message. Interval may only change after a successful uplink (e.g. future downlink will set the next interval in the TX-complete path).

**Payload:** Byte 0 = interval index (0–10). Bytes 1–2 = 12-bit battery offset + 4-bit sequence. Bytes 3–8 = six temperature slots. Index 0 = unused; indices 1–10 map to 1, 5, 15, 30, 60, 120, 360, 720, 1440, 10080 minutes.

**Firmware:** Added `kIntervalSecondsByIndex` table and `currentIntervalIndex` (default 2 = 5 min). Sleep duration is derived from the table. Payload built with interval byte first, then offset/sequence, then temperatures. `currentIntervalIndex` is only written in setup and (in future) after successful TX.

**Decoder:** Requires 9 bytes. Outputs `interval_index`, `interval_minutes`, `interval_label`. Parses offset/sequence from bytes 1–2 and temperatures from bytes 3–8. Protocol version set to 6.
