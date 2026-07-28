# INA219 register reference — every value this firmware can observe

Source: **TI SBOS448G** (INA219, rev G, Dec 2015), read directly from the
datasheet PDF, cross-checked against `Adafruit_INA219` 1.2.x as installed
(`~/Arduino/libraries/Adafruit_INA219`). Written 2026-07-28 after the
warm-reset misdetect (`fe7b533`) and the powerSave freeze (`007a46b`), both of
which were "a register value nobody had enumerated".

**The point of this document**: the INA219 has no error codes in the usual
sense. It has a config register whose value *is* its state, and two status
flags we currently throw away. Everything below is what those can read as.

---

## 1. Register map (Table 2, §8.6.1)

| Addr | Name | Type | POR value | Notes |
|---|---|---|---|---|
| `00h` | Configuration | R/W | **`399Fh`** | the probe's identity check |
| `01h` | Shunt voltage | R | — | 2's complement, LSB 10 µV |
| `02h` | Bus voltage | R | — | data in bits 15–3; **CNVR bit 1, OVF bit 0** |
| `03h` | Power | R | `0000h` | reading it **clears CNVR** |
| `04h` | Current | R | `0000h` | zero until Calibration is programmed |
| `05h` | Calibration | R/W | `0000h` | sets Current/Power full-scale |

**There is no manufacturer-ID or die-ID register.** The map ends at `05h`. This
is why the probe must identify the part by its config value — unlike the INA226
(die ID `2260h` at `FFh`) there is nothing else to ask. That is a constraint of
the part, not a shortcut in our code.

Datasheet footnote (2): *"The Power register and Current register default to 0
because the Calibration register defaults to 0, yielding a zero current value
until the Calibration register is programmed."*

Timing: register contents update **4 µs** after a write completes; a 4 µs
write-to-read gap is required only above 1 MHz SCL. We run 100 kHz and the probe
waits 1 ms, so this is a non-issue.

---

## 2. Configuration register `00h` — the values we can see

Bit layout (Figure 19): `RST`(15) `—`(14, unused) `BRNG`(13) `PG1 PG0`(12–11)
`BADC`(10–7) `SADC`(6–3) `MODE`(2–0).

- **RST (bit 15)**: *"generates a system reset that is the same as power-on
  reset. Resets all registers to default values; this bit self-clears."*
  Note **all** registers — including Calibration back to `0000h`.
- **BRNG (13)**: `0` = 16 V FSR, `1` = 32 V FSR (default).
- **PG (12–11)**: `00` = /1 ±40 mV, `01` = /2 ±80 mV, `10` = /4 ±160 mV,
  `11` = /8 ±320 mV (default).
- **BADC / SADC**: resolution or averaging. 9/10/11/12-bit = 84/148/276/**532 µs**
  (586 µs max); then 2/4/8/16/32/64/128 samples = 1.06/2.13/4.26/8.51/17.02/
  34.05/**68.10 ms**.
- **MODE (2–0)**: `000` power-down, `001` shunt triggered, `010` bus triggered,
  `011` shunt+bus triggered, `100` ADC off (disabled), `101` shunt continuous,
  `110` bus continuous, `111` shunt+bus continuous (default).

### Every config value this firmware produces

| Value | Meaning | When observed |
|---|---|---|
| **`399Fh`** | POR default: 32 V, /8, 12-bit, 12-bit, shunt+bus continuous | after RST — **what the probe requires** |
| **`019Fh`** | our calibration, **awake**: 16 V, /1, 12-bit, 12-bit, shunt+bus continuous | during a read |
| **`0198h`** | our calibration, **powered down** (MODE `000`) | **between reads — i.e. almost always** |
| `3998h` | 32 V calibration powered down | not us; listed for completeness |
| `0000h` | *sentinel, not a read* — probe found nothing, `g_probeConfig` never written | primary boards, and failed probes |
| `FFFFh` | device ACKs but returns all-ones (floating SDA, dead part) | flaky bus |

Two consequences worth internalising:

1. **`0198h`, not `019Fh`, is now the value a warm reset leaves behind.** Since
   `007a46b` the last thing every cycle does is `powerSave(true)`, so an INA219
   surviving an MCU reset holds MODE=`000`. The soft-reset probe handles it
   identically (RST → `399Fh`), but if the RST write is ever lost on a flaky
   bus, the stale value the probe reads is now `0198h`. The dev-note and
   `CLAUDE.md` both say `019Fh`; that was true before this morning.

2. **`setCalibration_32V_2A()` and `_32V_1A()` write exactly `399Fh`** — bit for
   bit the POR value. Had we picked either of those, the warm-reset misdetect
   could never have happened: the running config would have matched what the
   probe expects. Choosing `16V_400mA` (correct on measurement grounds — it buys
   0.1 mA/LSB instead of 0.8) is what created a config the probe didn't
   recognise. Worth remembering if the calibration is ever revisited.

### Why a warm MCU reset does not reset the INA219

**Power-on reset threshold: 2 V.** The INA219 only self-resets if its supply
falls below 2 V. An MCU RST-pin press, watchdog, or `NVIC_SystemReset()` leaves
the 3.3 V rail up, so the part keeps its configured register contents. That is
the whole mechanism behind the A1 misdetect, stated by the datasheet.

---

## 3. The two status flags we currently discard

`getBusVoltage_raw()` does `(value >> 3) * 4`, which **throws away bits 2, 1 and
0**. Bits 1 and 0 are the only status signalling the part has.

### CNVR — Conversion Ready (bus voltage register, bit 1)

> *"the INA219 Conversion Ready bit (CNVR) indicates when data from a conversion
> is available in the data output registers. The CNVR bit is set after all
> conversions, averaging, and multiplications are complete. CNVR will clear
> under the following conditions: 1.) Writing a new mode into the Operating Mode
> bits in the Configuration Register (except for Power-Down or Disable)
> 2.) Reading the Power Register"* — §8.6.3.2

This bit is a direct detector for the defect we shipped a fix for this morning,
and the clear-conditions line up exactly with our access pattern:

- `powerSave(false)` writes MODE=`111` → **clears CNVR** (it is not power-down
  or disable).
- `powerSave(true)` writes MODE=`000` → **does not clear CNVR** (explicitly
  excepted).
- We never read the Power register (`03h`), so nothing else clears it.

So `wake → CNVR goes 0 → poll until 1 → read` is unambiguous, and a part that
never converts (the frozen-reading failure) never sets CNVR. A blind delay
cannot tell those apart; CNVR can.

**Caveat on datasheet self-consistency**: the Feature Description (§8.3.1)
describes the same bit as living in a "Status register" cleared by "reading the
Status register", and mentions a convert pin. The INA219 has neither — that text
is inherited from the INA209/INA226 family. The device-specific register section
(§8.6.3.2, quoted above) is authoritative: it is the **Power** register whose
read clears CNVR. Polling by reading `02h` therefore does not self-clear. If we
ever start reading `03h`, that changes.

### OVF — Math Overflow (bus voltage register, bit 0)

> *"The Math Overflow Flag (OVF) is set when the Power or Current calculations
> are out of range. It indicates that current and power data may be
> meaningless."* — §8.6.3.2

In **our** configuration OVF should be unreachable in normal operation, which is
what makes it useful:

- Shunt clips before Current can overflow (see §4), so the Current path cannot
  reach full scale.
- Power LSB = 20 × current LSB = 1 mW → 65.5 W full scale, against a ≤ 6.4 W
  ceiling (16 V × 400 mA).

Therefore **OVF set means something structural is wrong** — most plausibly a
corrupted Calibration register. That is a live risk: `getCurrent_raw()` rewrites
Calibration on *every* current read (see §5), so a partial I²C write lands
directly in the register that scales both Current and Power. OVF is the canary
for exactly that, and it costs nothing — same register read as CNVR.

---

## 4. Silent saturation — the failure mode with no flag at all

Our config sets **PG = /1 (±40 mV)** and **BRNG = 0 (16 V)**.

| Limit | Value | Behaviour beyond it |
|---|---|---|
| Shunt FSR | ±40 mV → **±400 mA** across the breakout's 0.1 Ω shunt | Shunt register clips at ±4000 (`0FA0h`/`F060h`). **No flag.** |
| Bus FSR | 16 V | Bus register saturates. **No flag.** |
| Absolute max on VIN± | **26 V** | out of spec regardless of BRNG |

The panel draws ~30 mA, so there is ~13× headroom and this is currently
theoretical. It stops being theoretical the day a larger panel is fitted: the
current reading would peg at 400 mA and the harvest accumulator would integrate
a *plausible* wrong number — no fault, no symptom. Detection, if ever needed, is
`shunt register == 0FA0h`, not a status bit.

Related: **`SADC`/`BADC` averaging changes conversion time by 128×.** Our 12-bit
single-sample setting is 532 µs (586 µs max) per channel, ~1.06 ms for both; the
fix committed this morning waits 5 ms, a 4.3× margin. Switch either field to
128-sample averaging and the requirement becomes **68.10 ms** — the 5 ms wait
would silently return stale data and we would have reproduced the freeze by a
different route. (The bogus "~68 ms of averaging" comment I removed this morning
appears to have been someone reading that row of Table 5.) A CNVR poll is
immune to this class of change; a fixed delay is not.

---

## 5. Library behaviours that matter

- **`getCurrent_raw()` rewrites the Calibration register on every call**, with
  the comment *"Sometimes a sharp load will reset the INA219, which will reset
  the cal register."* Good: it makes a lost calibration self-healing. Also good
  for us: writing `05h` is **not** a config write, so it does not clear CNVR.
  Bad: it is one more chance per cycle to corrupt `05h` on a marginal bus —
  see OVF above.
- **`success()` reflects only the most recent register read.** Our current check
  is:
  ```c
  uint16_t busMv = ina219.getBusVoltage_V() * 1000;  // sets _success (bus read)
  float currentMa = ina219.getCurrent_mA();          // OVERWRITES _success (current read)
  g_ina219ReadOk = ina219.success() && (busMv < 20000);
  ```
  A failed bus read followed by a successful current read reports healthy. This
  is a real gap in the fix committed this morning (`007a46b`) — it should sample
  `success()` after *each* read and AND them.
- **`powerSave()` writes only MODE (3 bits)**, preserving BRNG/PG/BADC/SADC —
  so wake/sleep cannot silently change the calibration. Verified in the library
  via `Adafruit_BusIO_RegisterBits(&config_reg, 3, 0)`.

### powerSave is load-bearing, not an optimisation

| State | Quiescent current (typ / max) |
|---|---|
| Operating | **0.7 mA / 1 mA** |
| Power-down mode | **6 µA / 15 µA** |

Leaving the INA219 running costs ~0.7 mA continuous = **~16.8 mAh/day**, against
a documented harvest of 7–28 mAh/day (`solar_signal.h`). It would consume most
or all of the energy budget it exists to measure. This is why the fix had to be
"wake before reading" rather than "stop powering down".

---

## 6. Other bus-level behaviour

- **SMBus timeout: 28–35 ms.** *"SMBus timeout in the INA219 resets the interface
  any time SCL or SDA is low for over 28 ms."* A wedged bus self-recovers — so a
  single bad read should not be treated as a dead part (which `probeDecide()`
  already gets right: any one clean attempt out of three means present).
- Operating supply 3–5.5 V; POR threshold 2 V.

---

## 7. Recommendations, ranked

1. **Gate the read on CNVR instead of a fixed 5 ms delay.** Directly detects the
   non-converting part that cost us a night of telemetry, and stays correct if
   `BADC`/`SADC` are ever changed. Needs a raw 2-byte read of `02h` (the Adafruit
   accessor discards the flag) — the probe already does raw `Wire` I²C, so this
   fits the existing style. Timeout ~10 ms (8.5× the 1.172 ms worst case)
   → raise `DIAG_FAULT_INA219_READ_FAIL`.
2. **Check `success()` after each read, not just the last** (§5). Small, and it
   closes a hole in the current fix.
3. **Report OVF.** One bit from the same register read as CNVR; a calibration
   corruption canary that should never fire.
4. **Document, don't implement: shunt saturation** (§4). No headroom problem
   today; revisit if the panel changes.
5. **Optional probe hardening**: after RST, also read Calibration `05h` and
   require `0000h`. Turns a 16-bit identity match into 32-bit, guarding against
   a foreign device at `0x40` that happens to read `399Fh`. Low probability,
   very cheap.
