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
| INA219 `SDA` | Feather `SDA` (pin 20) | I2C, addr 0x40 |
| INA219 `SCL` | Feather `SCL` (pin 21) | |
| Battery pack (+) via PCM | Feather JST `+` | |
| Battery pack (−) via PCM | Feather JST `−` (GND) | |
| Supercap | across the pack (∥) | + to pack +, − to pack − |
| DS18B20 DATA | Feather `A2` | + 4.7 kΩ to 3V |
| DS18B20 VDD / GND | Feather `3V` / `GND` | |
| RFM95 `DIO1` | Feather pin `6` | jumper; LMIC needs it |
| Strap | pin `11` (float=PROD, GND=DEV) | |

## DS18B20 pinout (TO-92)

The bare sensor is a TO-92 package — one **flat** face, one **curved** back. Hold
it with the **flat face toward you and the legs pointing down**; the pins are then
`GND`, `DQ`, `VDD` from **left to right**.

```
     ___          flat face toward you, legs down
    /   \
   | DS  |        pin 1 (left)   GND   black wire
   |18B20|        pin 2 (mid)    DQ    yellow wire  → A2 (+4.7 kΩ to 3V)
   |flat |        pin 3 (right)  VDD   red wire
    | | |
    1 2 3
   G D V
   N Q D
   D   D
```

| Pin | Name | Probe wire | To |
|---|---|---|---|
| 1 (left) | `GND` | black | `GND` |
| 2 (middle) | `DQ` (data) | yellow | `A2` + 4.7 kΩ to `3V` |
| 3 (right) | `VDD` | red | `3V` |

Confirm the probe's wire colours before trusting them: most use red=VDD,
yellow=DQ, black=GND, but some bring data out as **white/blue** and cheap batches
occasionally swap red/black. Buzz each wire to its TO-92 leg if unsure — reversed
VDD/GND overheats the part and reads nothing.

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
PCM already on it; a cheap 1S protection board ("DW01 + 8205A") is the same thing
loose; a "BMS" is the same idea for multi-cell *series* packs (more than we need
at 1S).

**A charger is NOT a PCM.** A solar charger — CN3791, CN3065, or the Feather's
onboard MCP73831 — controls current *into* the cell and prevents over-*charge*.
It does nothing about over-*discharge*: with no sun it is idle while the load
drains the cell. So **non-protected cells always need a separate protection
board**, whatever charger is used. (Note also: common CN3791 modules are 12 V
input variants whose MPPT setpoint a 5 V panel can never reach — see the no-MPPT
decision in `docs/dev-notes/20260717-1220_no-mppt-decision.md`.)

**The protection board must be 1S, not 2S.** The pack is 1S2P — two cells in
**parallel**, one 3.7 V group — and the Feather's battery input takes a **single
cell only (3.0–4.2 V)**. A **1S** protection board has 4 pads (B+ B− P+ P−). A
**2S** board (e.g. marked "2S", with a **BM** / battery-middle midpoint pad, 5
pads) is for two cells in **series** at 7.4 V — wrong configuration, and wiring
2S would feed ~8 V into the JST and destroy the board. Buy a board that says
**1S** and has no midpoint pad.

**What to buy:** a standard **1S DW01 + 8205A** protection board — over-discharge
cutoff ~2.45 V (below the 3.4 V brownout, protects the cell), over-charge ~4.25 V,
4 pads (B+ B− P+ P−). Any current rating ≥ ~3 A is far more than the load needs
(µA asleep, ~120 mA on TX). For the **1S2P** pack, tie both cells' + to B+ and
both − to B−; the load (Feather + supercap) hangs off P+ / P−. Sold in cheap
multi-packs; grab spares.

**Bench vs field:** unprotected cells are fine on the bench *while attended* —
just keep them above ~3 V. For field deployment the protection board is
mandatory; the onboard MCP73831 handles over-charge, the board handles the
over-discharge cutoff.

**Bench charging.** A standalone 1S charger module (e.g. a TP4056 board, often
silkscreened "FC-75", with IN+/IN−/BAT+/BAT−) is handy for topping up bare cells
off USB between tests — never unattended. It is a *charger*, not part of the
deployed design (we charge through the onboard MCP73831) and not a protection
board unless it also has OUT+/OUT− pads (a DW01+8205A protected variant).

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
