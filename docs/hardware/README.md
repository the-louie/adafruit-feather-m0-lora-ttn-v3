# Hardware wiring — solar variant (gisebo-05)

How the Feather M0, INA219, panel, li-ion pack, and DS18B20 connect. The one
subtle part is the INA219: it goes **in the charge path** as a high-side current
sense, so it measures the solar harvest and the bus voltage the policy keys on.

## Overview schematic

```
  SOLAR CHARGE PATH  (high-side current sense; measures harvest + bus voltage)
  ===========================================================================

   ┌──────────────┐
   │ SOLAR PANEL  │  5 V 0.15 W, vertical, true south
   │   (+)   (−)  │
   └────┬─────┬───┘
        │     │
        ▼     │            Schottky 1N5817, BAND (cathode) toward the INA219.
    ──►|◄──   │            Blocks reverse leakage back into the panel at night.
        │     │
        ▼     │
   ┌──────────┴─────────────────────────┐
   │ INA219  (I2C addr 0x40)             │
   │                                     │
   │   Vin+ ◄── panel/Schottky (source)  │   current flows Vin+ ──► Vin−
   │   Vin− ──► Feather "USB" pin (load) │   (positive = charging)
   │                                     │
   │   VCC   GND   SDA   SCL             │
   └────┬─────┬─────┬─────┬──────────────┘
        │     │     │     │
        3V    GND   SDA   SCL   ─────────────► Feather I2C (SDA/SCL) + 3V + GND

        Vin− node ─────────────────────────► Feather "USB" pin
                                              (this is the onboard charger input;
                                               applying panel voltage here charges
                                               the pack via the MCP73831)


  BATTERY / STORAGE  (all grounds are common with the panel −)
  ===========================================================

   ┌───────────────┐
   │ 1S2P 18650    │────┬──── SUPERCAP 5.5 V ~1 F ────┐   (supercap ∥ the pack;
   │ li-ion pack   │    │                             │    covers the 120 mA TX
   │  (2 cells ∥)  │    │                             │    spike in the cold)
   └───────┬───────┘    └───────────┬─────────────────┘
           │                        │
        ┌──┴───┐                    │
        │ PCM  │  Protection Circuit├──(+)──► Feather JST +   (BAT)
        │ 2.4– │  Module: cuts the  │
        │ 2.8V │  pack off at ~2.5 V├───────  (over-discharge protection)
        │ cut  ├────────(−)─────────┴──(−)──► Feather JST −   (GND)
        └──────┘


  SENSOR + RADIO + MODE
  =====================

   ┌──────────┐   DATA ──┬─────────────────────────────► A2
   │ DS18B20  │   4.7k ──┘   (4.7 kΩ pull-up: DATA ↔ 3V)   [MANDATORY]
   │ (water)  │   VDD ───────── 3V
   │          │   GND ───────── GND
   └──────────┘

   RFM95 DIO1 (on-board) ──── jumper wire ──────────────► pin 6   [MANDATORY
                                                                   for LoRa]

   Strap:  pin 11  floating  = PROD  (FPort 11)
                   tied to GND = DEV  (FPort 21, no deep sleep, no USB conflict)
```

## Connection table (unambiguous reference)

| From | To | Note |
|---|---|---|
| Panel (+) | Schottky anode | |
| Schottky cathode (band) | INA219 `Vin+` | reverse-blocking |
| INA219 `Vin−` | Feather `USB` pin | charger input; also the bus-voltage sense node |
| Panel (−) | common GND | |
| INA219 `VCC` | Feather `3V` | |
| INA219 `GND` | Feather `GND` | |
| INA219 `SDA` | Feather `SDA` | I2C, addr 0x40 |
| INA219 `SCL` | Feather `SCL` | |
| Battery pack (+) via PCM | Feather JST `+` | |
| Battery pack (−) via PCM | Feather JST `−` (GND) | |
| Supercap | across the pack (∥) | + to pack +, − to pack − |
| DS18B20 DATA | Feather `A2` | + 4.7 kΩ to 3V |
| DS18B20 VDD / GND | Feather `3V` / `GND` | |
| RFM95 `DIO1` | Feather pin `6` | jumper; LMIC needs it |
| Strap | pin `11` (float=PROD, GND=DEV) | |

## The PCM (Protection Circuit Module)

A **PCM** is the small board between the li-ion cells and the load that protects
the pack. For our 1S pack its job is **over-discharge protection**: the Feather
browns out at ~3.4 V but keeps leaking tens of µA afterward, which over a long
dark winter spell would walk a cell down past ~2.5 V and destroy it. Firmware
cannot help — it is already off. The PCM disconnects the pack at ~2.4–2.8 V:
below the brownout, so firmware always acts first, but above the cell's damage
threshold, so the cell survives to recharge when the sun returns.

Use **one pack-level PCM across the 2P bank**, not two "protected cells" in
parallel — two protection circuits in parallel fight on recovery (one trips, the
other inherits the load and trips too). A single PCM sees the true pack voltage,
trips once, recovers once. Shopping terms: a "protected 18650" is a cell with a
PCM already on it; a "BMS" is the same idea for multi-cell *series* packs (more
than we need at 1S).

## Which parts you need for which test

| goal | panel | INA219 | Schottky | supercap | PCM |
|---|---|---|---|---|---|
| core bench (idle750, join, sleep) | – | – | – | – | – |
| solar sense on a bench (daylight) | ✔ | ✔ | rec. | – | – |
| correct night behaviour | ✔ | ✔ | **✔** | – | – |
| field deployment | ✔ | ✔ | ✔ | ✔ | **✔** |

The Schottky, supercap, and PCM are for correct field behaviour and cell safety.
For a first daylight bench read you can get away with just panel → INA219 →
Feather USB pin + I2C, but add the Schottky before trusting the night reading
(without it the bus voltage will not fall cleanly to ~0 in the dark).

## Cautions

- **JST polarity.** If you did not buy an Adafruit-wired pack, verify + and −
  against the board silkscreen before plugging in. Reversed polarity can destroy
  the board.
- **INA219 direction.** `Vin+` is the panel/source side, `Vin−` the Feather/load
  side. Get this backwards and `getCurrent_mA()` reads negative while charging
  (the firmware floors negatives to 0, so harvest would read 0 all day).
- **USB masks the panel.** With USB plugged in for programming, the `USB` pin is
  held at 5 V regardless of the panel, so the INA219 reads USB, not sun. To read
  real solar, run on battery and read telemetry over the air (FPort 11/21), or
  strap DEV with USB unplugged. Serial and real solar cannot coexist.
- **Supercap inrush.** A ~1 F cap across a fresh pack is a momentary short until
  it charges. Connect it with the pack via the PCM, or add a small series
  resistor during hookup, so the inrush does not trip protection or stress a cell.
- **Panel voltage.** A "5 V" panel has Voc ~6 V; the INA219 16 V range (firmware
  uses `setCalibration_16V_400mA()`, 0.1 mA/LSB) handles it comfortably.

## Firmware notes

- The variant probe finds the INA219 at 0x40 and boots the **solar** policy
  (FPorts 11/21, 15-byte payload). No INA219 → primary policy → li-ion misread as
  a flat 6 V pack → parks at a 7-day interval. That is the misdetect the backend
  alarm (S01-09) watches for.
- If the INA219 is not yet wired but you want to exercise the solar logic, the
  `SOLAR_NO_INA219` build option reads bus voltage from an ADC divider instead
  (see the main sketch). With the INA219 present, leave it off (the default).
