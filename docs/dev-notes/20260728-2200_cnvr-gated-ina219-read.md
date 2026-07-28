# INA219 reads gated on CNVR; OVF surfaced as a fault (item 18)

Date: 2026-07-28 22:00 CEST
Closes TODO item **18**. Firmware queues for the next flash; the decoder half is
already live (formatter re-uploaded, verified byte-identical).

## What changed

The fixed 5 ms post-wake wait is gone. The read path is now:

```
powerSave(false)                     -- MODE=111, which CLEARS CNVR
poll bus register 02h (raw)          -- until CNVR=1 or 10 ms budget expires
busMv  = from the same raw value     -- 13-bit field, 4 mV/LSB
ovf    = raw bit 0
current = getCurrent_mA()            -- Adafruit accessor, success() sampled after
ingest ONLY if the verdict passes    -- ina219LiveReadOk()
powerSave(true)
```

New pieces:

- **`ina219_bus.h`** — the register interpretation as pure, host-tested logic:
  CNVR/OVF extraction, the 4 mV/LSB math, the `INA219_CNVR_TIMEOUT_MS` budget,
  and `ina219LiveReadOk()` (I2C ACKs + CNVR + < 20 V, deliberately no lower
  bound — a dark panel at 0 mV is the sun signal working). 17 new host cases in
  `test_ina219_bus.cpp`, including the datasheet's 16 V FSR vector and the
  frozen 3852 mV value read honestly.
- **`ina219ReadBusRaw()`** in the `.ino` — raw Wire read of 02h, same mechanics
  as the probe. Required because `getBusVoltage_raw()` does `(value >> 3) * 4`,
  discarding the only status bits the part has.
- **`DIAG_FAULT_INA219_OVF` (0x0080)** — new fault bit in `diagnostics.h`, the
  decoder (`ina219_ovf`), and the TTN formatter, landed together per the
  three-places rule. Guarded on solar + present like the read-fail bit.

## The judgement calls, recorded

**CNVR timeout faults reuse `DIAG_FAULT_INA219_READ_FAIL`** rather than a new
bit — "present but not converting" is a flavour of "live read bad", the decoder
needs no case for it, and the distinct-bit option buys little. (The TODO left
this open; resolved to reuse.)

**A failed read is NOT ingested.** The old path fed whatever came back into the
EWMA and harvest; that is exactly the overnight failure mode. Now a failed
verdict skips `ingestSample()` entirely: the interval simply is not integrated,
`lastBusMv_`/`lastCurrentMa_` keep the last good sample for the payload, and the
fault frame says why. Integrating garbage is strictly worse than integrating
nothing.

**OVF zeroes the current but not the verdict.** Overflow means the Current/Power
*calculations* are meaningless; bus voltage is a direct ADC result and stays
valid. So under OVF the EWMA keeps its (valid) voltage input, the harvest
accumulator gets 0 mA instead of garbage, and the fault bit reports the
structural problem — most plausibly a corrupted Calibration register, which
`getCurrent_raw()` rewrites on every read.

**Why CNVR beats any delay, restated once**: the clear conditions fit this
firmware exactly — `powerSave(false)` clears it, `powerSave(true)` is excepted
by the datasheet, and only a Power-register read (never performed) clears it
otherwise. And if `BADC`/`SADC` are ever moved to 128-sample averaging
(68.1 ms), a 10 ms budget **faults loudly every wake** instead of silently
serving stale data — misconfiguration becomes visible instead of becoming a
repeat of 2026-07-27.

## Verification

- Compiles **73700 B (28%)**; all host suites green (including the new
  `test_ina219_bus`, 17 cases) and 3 new `diagComputeFaults` cases (OVF alone,
  OVF + read-fail non-masking, primary-guard); decoder 39/39.
- Formatter re-uploaded to TTN, verified byte-identical to the repo file.
- Over the air after the next flash: behaviour is identical on a healthy unit
  (CNVR sets in ~1 ms). The new paths only speak when something is wrong:
  pulling SDA or holding the part in power-down must raise
  `ina219_read_fail` within one cycle instead of freezing silently.
