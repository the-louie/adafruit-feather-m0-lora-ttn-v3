# Persistent state across reset (S03-04, S03-05, S03-07, S03-08)

## What and why

The PROD join-failure path deep-sleeps 15 minutes then calls `NVIC_SystemReset()`. That re-runs C startup and zeroes `.bss`, wiping season state, the interval index, counters, and (once solar lands) all harvest history. Every such reset would otherwise restart at Summer/index 2 and re-walk the season machine one step per uplink — in winter, precisely when joins fail most and the wrong policy costs most.

`PersistState` lives in `.noinit` and survives the reset at **zero flash cost**. A soft reset does not physically clear SRAM; only the C runtime zeroes `.bss`, and `__attribute__((section(".noinit")))` opts out of that. This honours "Eradicate FlashStorage Entirely" (master-plan) rather than bending it — no flash wear, no SPI-bus disturbance.

## Guarded by magic + version + **CRC**

The CRC is the part that is easy to skip and must not be:

- **magic** catches uninitialised SRAM on a true cold boot.
- **version** catches a firmware upgrade that changed the layout.
- **CRC** catches *corruption* — and this design leans on a physical claim, that SRAM survives a soft reset. A **brief power interruption decays RAM partially**: long enough to corrupt the body, short enough to leave a 32-bit magic word standing. Magic + version alone would restore from garbage. The CRC over the body closes it.

17 host assertions, including the decisive one: magic and version intact, one body byte flipped, `persistValid` returns false. `S06-06` tests the physical version on hardware — a real brief power interruption — and without the CRC there is no plan for when that test fails.

## What is stored, and what is deliberately not

Stored: season state, latched voltage-band state (S02-19 made `voltage_offset` stateful), interval index, uplink counter, boot counter, clock-valid, harvest accumulator, surface temp, RTC epoch.

**Not stored: `dataBuffer` and `ramCount`.** Up to six buffered samples are lost on every reset, and that is correct. The decoder extrapolates per-sample timestamps assuming uniform spacing at byte 0's interval. A buffer straddling a reset carries a multi-interval hole (3 min of join attempts + 15 min sleep + rejoin) that byte 0 cannot express. Preserving it would hand the backend confidently mis-timestamped data. Dropping it costs six samples; the rejoin's fast-flush restarts the series cleanly.

## The bump-on-change rule

`PERSIST_VERSION` must be incremented on **any** change to the body layout, or a firmware upgrade will read the old layout as valid and resume from a struct that means something different — strictly worse than a cold boot. The host test pins `PERSIST_HEADER_BYTES` against the real offset so a header change is caught at build time, not in the field.
