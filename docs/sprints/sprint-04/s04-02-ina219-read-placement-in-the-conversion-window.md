# S04-02 — INA219 read placement in the conversion window

**Estimate:** 2 h
**Backlog item:** TODO #8
**Depends on:** S04-01, S02-01
**Needs hardware:** no

## Context

The DS18B20 needs 750 ms and the MCU is already spending it — reading I2C there is nearly free. **The exact placement depends on how S02-01 fixed `idle(750)`**, so this task cannot be written until that lands.

Sampling every wake, not once per uplink, is what makes a clockless day/night estimate work at all: one sample per message at a 30-minute interval would be one sample every 3 hours at an arbitrary time.

## Steps

1. Order: `requestTemperatures()` → wake INA219 → trigger conversion → read → power down → spend the remaining conversion time.
2. The INA219's own conversion takes ~532 µs by default, more with averaging — budget for it inside the window.
3. If S02-01 chose `delay(750)`, this is trivial. If it chose 9-bit resolution (94 ms), the window is tighter — check the INA219 read still fits.

## Done when

- [ ] INA219 read every wake, inside the existing conversion window.
- [ ] No added awake time.
- [ ] Works with whichever fix S02-01 chose.
