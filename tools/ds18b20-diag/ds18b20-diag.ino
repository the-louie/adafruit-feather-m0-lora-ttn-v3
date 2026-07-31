// DS18B20 bench diagnostic -- Adafruit Feather M0 (any variant), DQ on A0.
//
// Standalone sensor-and-cable tester: no LoRa, no LMIC, no project headers.
// Built to answer "is this sensor assembly good?" from the MCU end alone,
// after DC tests (DMM diode/leakage checks) have said the wiring LOOKS intact.
//
// Wiring: sensor VDD -> 3V3, GND -> GND, DQ -> A0, and a 4.7k resistor from
// A0 to 3V3 (the bare Feather has no pull-up on A0; the bus cannot idle high
// without one, and phase 0 below will say so).
//
// What it does:
//   0. Bus electrical pre-check: idle level (pull-up present? bus shorted?)
//      and rise time after release (weak pull-up / high capacitance / leakage).
//   1. Search until at least one device answers, reporting attempts while
//      waiting -- so a hot-plugged or wiggled-back-to-life sensor is caught.
//   2. Full per-device diagnostic: ROM + CRC, family code, power-supply mode
//      (a 3-wire sensor reporting PARASITE means its VDD conductor is open --
//      the one cable fault a DMM diode test cannot separate from healthy),
//      scratchpad dump + CRC, conversion at every resolution with the real
//      conversion time measured against the datasheet maximum, cross-
//      resolution consistency, an EEPROM write/recall test, a presence and
//      scratchpad-CRC soak for marginal-bus errors, and a 10-sample noise
//      profile.
//   3. Continuous monitor at 2 s cadence with cumulative error counters.
//      Wiggle the cable and connector here: intermittents that pass every
//      static test show up as presence losses or CRC errors on the counters.
//      If the device vanishes it drops back to phase 1 and re-runs everything
//      on reappearance.
//
// Serial: 115200 (the native USB CDC ignores the baud rate; any terminal
// setting works). Waits up to ~8 s for a monitor, then runs regardless.

#include <OneWire.h>

#define ONEWIRE_PIN A0
#define LED_PIN     13

// DS18B20 function commands (datasheet, "ROM commands" / "function commands").
#define CMD_CONVERT_T        0x44
#define CMD_WRITE_SCRATCHPAD 0x4E
#define CMD_READ_SCRATCHPAD  0xBE
#define CMD_COPY_SCRATCHPAD  0x48
#define CMD_RECALL_E2        0xB8
#define CMD_READ_POWER       0xB4

#define MAX_DEVICES 8

OneWire ow(ONEWIRE_PIN);

static uint8_t roms[MAX_DEVICES][8];
static uint8_t deviceCount = 0;
static bool    parasitic[MAX_DEVICES];

// Monitor-phase cumulative counters.
static uint32_t monReads = 0, monCrcErrors = 0, monPresenceLoss = 0;
static uint32_t mon85Events = 0;

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

static void printHex8(uint8_t b) {
  if (b < 0x10) Serial.print('0');
  Serial.print(b, HEX);
}

static void printRom(const uint8_t *rom) {
  for (uint8_t i = 0; i < 8; i++) { printHex8(rom[i]); if (i < 7) Serial.print(' '); }
}

static const char *familyName(uint8_t code) {
  switch (code) {
    case 0x28: return "DS18B20";
    case 0x10: return "DS18S20 (older part, different scratchpad!)";
    case 0x22: return "DS1822";
    case 0x3B: return "MAX31850";
    default:   return "UNKNOWN family";
  }
}

// Read the 9-byte scratchpad for one device. Returns true if the CRC matched.
static bool readScratchpad(const uint8_t *rom, uint8_t *sp) {
  if (!ow.reset()) return false;
  ow.select((uint8_t *)rom);
  ow.write(CMD_READ_SCRATCHPAD);
  for (uint8_t i = 0; i < 9; i++) sp[i] = ow.read();
  return OneWire::crc8(sp, 8) == sp[8];
}

// Write TH, TL and config, which is all 0x4E accepts, in that order.
static void writeScratchpad(const uint8_t *rom, uint8_t th, uint8_t tl, uint8_t cfg) {
  ow.reset();
  ow.select((uint8_t *)rom);
  ow.write(CMD_WRITE_SCRATCHPAD);
  ow.write(th); ow.write(tl); ow.write(cfg);
}

// Start a conversion and wait for completion. In external-power mode the
// device answers read slots with 0 while busy, so the ACTUAL conversion time
// is measurable; in parasite mode the bus must be held high instead, so the
// wait is the fixed datasheet maximum and the returned time is only an upper
// bound. Returns conversion time in ms, or 0 on presence failure.
static uint32_t convert(const uint8_t *rom, bool parasite, uint32_t timeoutMs) {
  if (!ow.reset()) return 0;
  ow.select((uint8_t *)rom);
  ow.write(CMD_CONVERT_T, parasite ? 1 : 0);
  uint32_t t0 = millis();
  if (parasite) {
    delay(timeoutMs);
    return timeoutMs;
  }
  while (millis() - t0 < timeoutMs) {
    if (ow.read_bit()) return millis() - t0;
    delay(1);
  }
  return timeoutMs;  // ran to the cap: caller compares against the datasheet max
}

// Temperature from a scratchpad, masking the bits the datasheet leaves
// undefined below the configured resolution.
static float scratchpadTempC(const uint8_t *sp) {
  int16_t raw = (int16_t)((sp[1] << 8) | sp[0]);
  uint8_t cfg = (sp[4] >> 5) & 0x03;           // 0=9bit .. 3=12bit
  raw &= (int16_t)~(0x07 >> cfg);              // 9bit: low 3 undefined, 10: 2, 11: 1
  return raw / 16.0f;
}

// ---------------------------------------------------------------------------
// Phase 0: bus electrics, no device required
// ---------------------------------------------------------------------------

static void busElectricalCheck() {
  Serial.println(F("--- phase 0: bus electrical pre-check ---"));

  // Idle level. OneWire idles as INPUT; with a pull-up fitted and no short,
  // the pin must read high on its own.
  pinMode(ONEWIRE_PIN, INPUT);
  delayMicroseconds(100);
  bool idleHigh = digitalRead(ONEWIRE_PIN);
  Serial.print(F("bus idle level : "));
  if (idleHigh) {
    Serial.println(F("HIGH (pull-up present, no hard short)"));
  } else {
    Serial.println(F("LOW  ** FAULT: missing 4.7k pull-up to 3V3, or DQ/GND short."));
    Serial.println(F("               Nothing can enumerate until this is fixed. **"));
  }

  // Rise time: drive low, release, count microseconds until the pull-up wins.
  // With 4.7k and typical cable capacitance this is a few us; tens of us means
  // a weak pull-up or a long/leaky cable; hundreds means real leakage. The
  // digitalRead loop quantises to ~1 us on the M0, good enough to classify.
  pinMode(ONEWIRE_PIN, OUTPUT);
  digitalWrite(ONEWIRE_PIN, LOW);
  delayMicroseconds(50);
  noInterrupts();
  pinMode(ONEWIRE_PIN, INPUT);
  uint32_t t0 = micros();
  while (!digitalRead(ONEWIRE_PIN) && (micros() - t0) < 5000) {}
  uint32_t rise = micros() - t0;
  interrupts();
  Serial.print(F("bus rise time  : "));
  Serial.print(rise);
  if (rise >= 5000)      Serial.println(F(" us (never rose -- see idle-level fault above)"));
  else if (rise > 200)   Serial.println(F(" us ** heavy leakage or extreme capacitance **"));
  else if (rise > 30)    Serial.println(F(" us (marginal: weak pull-up, long cable, or some leakage)"));
  else                   Serial.println(F(" us (healthy)"));
  Serial.println();
}

// ---------------------------------------------------------------------------
// Phase 1: search until something answers
// ---------------------------------------------------------------------------

static void searchUntilFound() {
  Serial.println(F("--- phase 1: searching for devices (plug/wiggle now if needed) ---"));
  uint32_t attempts = 0;
  for (;;) {
    deviceCount = 0;
    ow.reset_search();
    uint8_t rom[8];
    while (deviceCount < MAX_DEVICES && ow.search(rom)) {
      memcpy(roms[deviceCount], rom, 8);
      deviceCount++;
    }
    if (deviceCount > 0) break;

    attempts++;
    digitalWrite(LED_PIN, (attempts & 1) ? HIGH : LOW);
    if (attempts % 4 == 1) {
      Serial.print(F("no presence pulse yet (attempt "));
      Serial.print(attempts);
      Serial.print(F(", bus idle "));
      pinMode(ONEWIRE_PIN, INPUT);
      Serial.print(digitalRead(ONEWIRE_PIN) ? F("HIGH") : F("LOW"));
      Serial.println(F(")"));
    }
    delay(500);
  }
  digitalWrite(LED_PIN, LOW);

  Serial.print(F("found "));
  Serial.print(deviceCount);
  Serial.println(deviceCount == 1 ? F(" device:") : F(" devices:"));
  for (uint8_t d = 0; d < deviceCount; d++) {
    Serial.print(F("  ["));
    Serial.print(d);
    Serial.print(F("] "));
    printRom(roms[d]);
    Serial.print(F("  ("));
    Serial.print(familyName(roms[d][0]));
    Serial.print(F(", ROM CRC "));
    Serial.print(OneWire::crc8(roms[d], 7) == roms[d][7] ? F("ok") : F("** BAD **"));
    Serial.println(F(")"));
  }
  Serial.println();
}

// ---------------------------------------------------------------------------
// Phase 2: full diagnostic for one device
// ---------------------------------------------------------------------------

static void diagnoseDevice(uint8_t d) {
  const uint8_t *rom = roms[d];
  uint8_t sp[9];

  Serial.print(F("--- phase 2: diagnostics for ["));
  Serial.print(d);
  Serial.print(F("] "));
  printRom(rom);
  Serial.println(F(" ---"));

  // -- power-supply mode -----------------------------------------------------
  // THE key test for a suspect cable: a sensor wired 3-wire that answers
  // "parasite" is running off DQ charge because its VDD conductor is open at
  // the far end -- electrically invisible to a DMM diode test, and it still
  // converts, just less reliably (especially at temperature extremes).
  ow.reset();
  ow.select((uint8_t *)rom);
  ow.write(CMD_READ_POWER);
  bool external = ow.read_bit();
  parasitic[d] = !external;
  Serial.print(F("power supply   : "));
  if (external) {
    Serial.println(F("EXTERNAL (VDD conductor good)"));
  } else {
    Serial.println(F("PARASITE ** if you wired VDD to 3V3, the VDD conductor is"));
    Serial.println(F("            OPEN at the sensor end -- cable/joint fault **"));
  }

  // -- scratchpad ------------------------------------------------------------
  bool crcOk = readScratchpad(rom, sp);
  Serial.print(F("scratchpad     : "));
  for (uint8_t i = 0; i < 9; i++) { printHex8(sp[i]); Serial.print(' '); }
  Serial.print(F(" CRC "));
  Serial.println(crcOk ? F("ok") : F("** BAD **"));
  if (!crcOk) {
    Serial.println(F("aborting this device: cannot trust anything past a bad scratchpad read"));
    return;
  }

  int16_t rawNow = (int16_t)((sp[1] << 8) | sp[0]);
  Serial.print(F("power-on state : "));
  if (rawNow == 0x0550) Serial.println(F("temp register holds 85.0 C -- no conversion since power-up (normal on first contact)"));
  else                  Serial.println(F("temp register holds a prior conversion"));

  Serial.print(F("TH/TL/config   : "));
  printHex8(sp[2]); Serial.print('/'); printHex8(sp[3]); Serial.print('/'); printHex8(sp[4]);
  Serial.print(F("  (resolution "));
  Serial.print(9 + ((sp[4] >> 5) & 0x03));
  Serial.println(F("-bit)"));

  // Reserved-byte heuristic only: byte 5 reads 0xFF and byte 7 reads 0x10 on
  // genuine parts; clones frequently differ. Informational, not a verdict.
  Serial.print(F("die heuristic  : byte5="));
  printHex8(sp[5]);
  Serial.print(F(" byte7="));
  printHex8(sp[7]);
  Serial.println((sp[5] == 0xFF && sp[7] == 0x10)
                 ? F("  (matches genuine-part pattern)")
                 : F("  (differs from the usual genuine pattern -- possible clone, not necessarily bad)"));

  const uint8_t origTh = sp[2], origTl = sp[3], origCfg = sp[4];

  // -- conversion at every resolution ---------------------------------------
  // Measured against the datasheet maxima (93.75/187.5/375/750 ms). A part
  // near or over the max is degrading; one converting suspiciously fast at
  // 12-bit is usually a clone (again: different, not necessarily broken).
  static const uint16_t maxMs[4] = {94, 188, 375, 750};
  float temps[4];
  Serial.println(F("conversion sweep:"));
  for (uint8_t r = 0; r < 4; r++) {
    writeScratchpad(rom, origTh, origTl, (uint8_t)(0x1F | (r << 5)));
    uint32_t ms = convert(rom, parasitic[d], 1000);
    bool rOk = readScratchpad(rom, sp);
    temps[r] = rOk ? scratchpadTempC(sp) : NAN;
    Serial.print(F("  "));
    Serial.print(9 + r);
    Serial.print(F("-bit: "));
    if (!rOk) { Serial.println(F("** scratchpad CRC failed after conversion **")); continue; }
    Serial.print(temps[r], 4);
    Serial.print(F(" C in "));
    if (parasitic[d]) {
      Serial.print(F("<="));
      Serial.print(ms);
      Serial.println(F(" ms (parasite: fixed wait, not measurable)"));
    } else {
      Serial.print(ms);
      Serial.print(F(" ms (datasheet max "));
      Serial.print(maxMs[r]);
      Serial.print(F(" ms)"));
      if (ms >= maxMs[r])            Serial.println(F(" ** AT/OVER MAX -- degraded or dying **"));
      else if (r == 3 && ms < 300)   Serial.println(F("  (very fast -- clone-typical)"));
      else                           Serial.println();
    }
  }
  // Cross-resolution agreement: all four measured the same physical
  // temperature, so after undefined-bit masking they must agree within the
  // coarsest step (0.5 C at 9-bit).
  if (temps[0] == temps[0] && temps[3] == temps[3]) {
    float d93 = temps[3] - temps[0]; if (d93 < 0) d93 = -d93;
    Serial.print(F("  9 vs 12-bit agreement: "));
    Serial.print(d93, 4);
    Serial.println(d93 <= 0.5f ? F(" C (ok)") : F(" C ** INCONSISTENT -- ADC fault **"));
  }

  // -- EEPROM write / recall -------------------------------------------------
  // Proves the internal EEPROM path: write sentinel TH/TL, copy to EEPROM,
  // recall E2 (which overwrites the scratchpad from EEPROM), verify, then
  // restore the original values the same way. Two EEPROM write cycles per run
  // against a 50k-cycle endurance rating: negligible wear.
  Serial.print(F("EEPROM test    : "));
  writeScratchpad(rom, 0x55, 0xAA, origCfg);
  ow.reset(); ow.select((uint8_t *)rom); ow.write(CMD_COPY_SCRATCHPAD, 1);
  delay(12);
  writeScratchpad(rom, 0x00, 0x00, origCfg);       // scribble, so recall is provable
  ow.reset(); ow.select((uint8_t *)rom); ow.write(CMD_RECALL_E2);
  { uint32_t t0 = millis(); while (!ow.read_bit() && millis() - t0 < 50) {} }
  bool eepOk = readScratchpad(rom, sp) && sp[2] == 0x55 && sp[3] == 0xAA;
  Serial.println(eepOk ? F("write/copy/recall ok") : F("** FAILED -- EEPROM or logic fault **"));
  writeScratchpad(rom, origTh, origTl, origCfg);   // leave the device as found
  ow.reset(); ow.select((uint8_t *)rom); ow.write(CMD_COPY_SCRATCHPAD, 1);
  delay(12);

  // -- soak: presence + scratchpad CRC --------------------------------------
  // Static tests pass on marginal buses; error RATE is what exposes them.
  Serial.print(F("presence soak  : "));
  uint16_t presenceFail = 0;
  for (uint16_t i = 0; i < 100; i++) { if (!ow.reset()) presenceFail++; delay(2); }
  Serial.print(100 - presenceFail);
  Serial.println(F("/100 resets answered"));

  Serial.print(F("CRC soak       : "));
  uint16_t crcFail = 0;
  for (uint16_t i = 0; i < 50; i++) { if (!readScratchpad(rom, sp)) crcFail++; }
  Serial.print(50 - crcFail);
  Serial.print(F("/50 clean scratchpad reads"));
  Serial.println((presenceFail == 0 && crcFail == 0)
                 ? F("  (bus solid)")
                 : F("  ** ERRORS: marginal bus -- leakage, weak pull-up, or bad joint **"));

  // -- noise profile ---------------------------------------------------------
  // Ten 12-bit conversions of a thermally still sensor: peak-to-peak spread
  // beyond a few LSB (~0.06 C each) means electrical noise -- supply trouble,
  // leakage, or a failing die. A drifting sensor (just handled, sunlit) will
  // show more; judge accordingly.
  writeScratchpad(rom, origTh, origTl, 0x7F);
  float mn = 1000, mx = -1000, sum = 0;
  uint8_t good = 0;
  for (uint8_t i = 0; i < 10; i++) {
    convert(rom, parasitic[d], 1000);
    if (!readScratchpad(rom, sp)) continue;
    float t = scratchpadTempC(sp);
    if (t < mn) mn = t;
    if (t > mx) mx = t;
    sum += t; good++;
  }
  writeScratchpad(rom, origTh, origTl, origCfg);
  Serial.print(F("noise profile  : "));
  if (good == 0) {
    Serial.println(F("** no clean reads **"));
  } else {
    Serial.print(good);
    Serial.print(F("/10 reads, mean "));
    Serial.print(sum / good, 3);
    Serial.print(F(" C, p-p "));
    Serial.print(mx - mn, 3);
    Serial.print(F(" C"));
    Serial.println((mx - mn) <= 0.125f ? F("  (quiet)") : F("  (elevated -- noise or genuine drift)"));
  }

  // -- plausibility ----------------------------------------------------------
  if (good > 0) {
    float mean = sum / good;
    Serial.print(F("plausibility   : "));
    if (mean <= -55.0f || mean >= 125.0f)      Serial.println(F("** outside device range **"));
    else if (mean == 85.0f)                    Serial.println(F("** stuck at power-on default **"));
    else if (mean < -20.0f || mean > 50.0f)    Serial.println(F("odd for a bench -- check setup"));
    else                                       Serial.println(F("sane bench-ambient reading"));
  }
  Serial.println();
}

// ---------------------------------------------------------------------------
// Phase 3: continuous monitor (wiggle-test the cable here)
// ---------------------------------------------------------------------------

static bool monitorTick() {
  for (uint8_t d = 0; d < deviceCount; d++) {
    uint8_t sp[9];
    uint32_t ms = convert(roms[d], parasitic[d], 1000);
    if (ms == 0) { monPresenceLoss++; return false; }   // no presence: device gone
    monReads++;
    if (!readScratchpad(roms[d], sp)) {
      monCrcErrors++;
      Serial.print(F("["));
      Serial.print(d);
      Serial.println(F("] CRC ERROR"));
      continue;
    }
    float t = scratchpadTempC(sp);
    int16_t raw = (int16_t)((sp[1] << 8) | sp[0]);
    if (raw == 0x0550) mon85Events++;
    Serial.print(F("["));
    Serial.print(d);
    Serial.print(F("] "));
    Serial.print(t, 4);
    Serial.print(F(" C  ("));
    Serial.print(ms);
    Serial.print(F(" ms, reads "));
    Serial.print(monReads);
    Serial.print(F(", crcErr "));
    Serial.print(monCrcErrors);
    Serial.print(F(", presenceLoss "));
    Serial.print(monPresenceLoss);
    Serial.print(F(", 85C-events "));
    Serial.print(mon85Events);
    Serial.println(F(")"));
    if (raw == 0x0550) Serial.println(F("    ^ power-on default: the die reset since the last conversion (supply glitch?)"));
  }
  return true;
}

// ---------------------------------------------------------------------------

void setup() {
  pinMode(LED_PIN, OUTPUT);
  Serial.begin(115200);
  uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 8000) {
    digitalWrite(LED_PIN, (millis() / 125) & 1);   // fast blink: waiting for monitor
    delay(10);
  }
  digitalWrite(LED_PIN, LOW);
  Serial.println();
  Serial.println(F("=== DS18B20 bench diagnostic (DQ on A0, 4.7k pull-up to 3V3 required) ==="));
  Serial.println();
}

void loop() {
  busElectricalCheck();
  searchUntilFound();
  for (uint8_t d = 0; d < deviceCount; d++) diagnoseDevice(d);

  Serial.println(F("--- phase 3: continuous monitor, 2 s cadence -- wiggle the cable now ---"));
  Serial.println(F("    (an intermittent fault shows as presenceLoss/crcErr counting up)"));
  while (monitorTick()) {
    digitalWrite(LED_PIN, HIGH); delay(30); digitalWrite(LED_PIN, LOW);
    delay(2000);
  }

  Serial.println();
  Serial.println(F("** DEVICE LOST -- back to search; diagnostics re-run on reappearance **"));
  Serial.println();
}
