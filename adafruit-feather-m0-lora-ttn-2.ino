/*
 * Two-phase flow: Commissioning (LMIC.devaddr == 0) spins loop with
 * os_runloop_once() until joined. Operational: read sensors, send batch when
 * full, then deep sleep. Application logic does not use OS jobs; TX completion
 * is waited on synchronously.
 */
#include <lmic.h>

// -----------------------------------------------------------------------------
// COMPILE-TIME REGION ASSERTION
// -----------------------------------------------------------------------------
#ifndef CFG_eu868
#error                                                                         \
    "FATAL: This firmware explicitly requires CFG_eu868. Update lmic_project_config.h"
#endif

#include <ArduinoLowPower.h>
#include <DallasTemperature.h>
#include <OneWire.h>
#include <SPI.h>
#include <Wire.h>
#include <RTCZero.h>
#include <hal/hal.h>
#include "uplink_schedule.h"
#include "season.h"
#include "power_policy.h"
#include "policy_primary.h"
#include "payload.h"
#include "persist.h"
#include "variant_probe.h"
#include "timekeeping.h"
#include "policy_solar.h"
#include <Adafruit_INA219.h>

#define VBATPIN A7

// Two dummy reads let the SAMD21 ADC sampling capacitor settle through the
// 100k/100k divider, then average 16 to beat the noise down.
//
// A SINGLE sample carries ~+/-19 mV (6.45 mV LSB), which is enough to flip a
// voltage band every wake for a pack sitting on an edge -- and on the solar
// variant that edge gates the solar bonus. Averaging 16 cuts the noise 4x at a
// cost of microseconds, inside a wake that now deliberately spends 750 ms.
// This attacks the dither at source; voltageOffsetHyst() absorbs what is left.
// They are complements, not alternatives.
#define VBAT_SAMPLES 16
static float getBatteryVoltage(void) {
  analogRead(VBATPIN);
  analogRead(VBATPIN);
  uint32_t sum = 0;
  for (uint8_t i = 0; i < VBAT_SAMPLES; i++) {
    sum += analogRead(VBATPIN);
  }
  return ((float)sum / VBAT_SAMPLES) * (2.0f * 3.3f / 1024.0f);
}

#define VBAT_VOLTS() getBatteryVoltage()

#define STRAP_PIN 11
#define LED_PIN 13
#define ONE_WIRE_BUS A2

// ---------------------------------------------------------------------------
// Bench option: run the SOLAR policy WITHOUT an INA219 (S07 stopgap)
// ---------------------------------------------------------------------------
// The solar interval logic keys on panel BUS VOLTAGE, not current -- current
// only feeds the harvest accumulator (telemetry). So a plain resistor divider on
// a spare ADC pin gives the whole control path; you lose only the harvest field.
//
// Wire:  panel(+) --[R1]--+--[R2]-- GND,  tap the middle node to PANEL_ADC_PIN.
// With R1=200k, R2=100k that is a /3 divider: a 6.0 V panel -> 2.0 V at the pin,
// safely below the 3.3 V ADC reference. Panel(-) and the Feather share GND.
//
// Uncomment SOLAR_NO_INA219 to enable. Default OFF, so the normal INA219 build
// is unchanged. When enabled the probe is bypassed and the device is solar.
//#define SOLAR_NO_INA219
#define PANEL_ADC_PIN   A1
#define PANEL_DIV_RATIO 3.0f   // (R1 + R2) / R2

// Safely size the buffer to the protocol maximum to prevent future memmove
// overflows
#define MAX_BATCH 6

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);


// -----------------------------------------------------------------------------
// TTN OTAA CREDENTIALS (From your known-good config)
// -----------------------------------------------------------------------------
// AppEUI: Little-endian (LSB first)
static const u1_t PROGMEM APPEUI[8] = {0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
void os_getArtEui(u1_t *buf) { memcpy_P(buf, APPEUI, 8); }

static const u1_t PROGMEM DEVEUI[8] = {0x82, 0x88, 0x07, 0xD0, 0x7E, 0xD5, 0xB3, 0x70}; // 5 (gisebo-05, solar v7) = 70B3D57ED0078882
//static const u1_t PROGMEM DEVEUI[8] = {0xDD, 0x57, 0x07, 0xD0, 0x7E, 0xD5, 0xB3, 0x70}; // 1 (PRODUCTION, frozen — DO NOT flash from v7)
//static const u1_t PROGMEM DEVEUI[8] = {0x01, 0x5E, 0x07, 0xD0, 0x7E, 0xD5, 0xB3, 0x70}; // 4
void os_getDevEui(u1_t *buf) { memcpy_P(buf, DEVEUI, 8); }

// AppKey: Big-endian (MSB first)
static const u1_t PROGMEM APPKEY[16] = {0xC4, 0x63, 0xCB, 0xFE, 0xD2, 0x86, 0xED, 0x81, 0x84, 0xA4, 0x5A, 0x3C, 0x3D, 0xB3, 0x69, 0x38}; // 5 (gisebo-05, solar v7)
//static const u1_t PROGMEM APPKEY[16] = {0x79, 0x87, 0x8E, 0x16, 0x19, 0xDA, 0xF4, 0x3C, 0xB6, 0x76, 0x71, 0xFC, 0x54, 0x68, 0xBE, 0xDB}; // 1 (PRODUCTION, frozen — DO NOT flash from v7)
//static const u1_t PROGMEM APPKEY[16] = {0x10, 0x14, 0x8B, 0x3A, 0x38, 0x5D, 0x46, 0xDA, 0xB3, 0x1E, 0xB6, 0x08, 0x1B, 0xD9, 0x26, 0x46}; // 4




void os_getDevKey(u1_t *buf) { memcpy_P(buf, APPKEY, 16); }

// Interval index table: 0 = unused (5 min default if ever read), 1-10 = 1,5,15,30,60,120,360,720,1440,10080 minutes (in seconds)
static const uint32_t kIntervalSecondsByIndex[11] = {
    300, 60, 300, 900, 1800, 3600, 7200, 21600, 43200, 86400, 604800
};

// Season thresholds and base indices now live in season.h.
// Voltage bands and the interval clamp live in power_policy.h / policy_primary.h.

// Application state (set once in setup from STRAP_PIN)
static uint8_t runMode; // 0 = PROD, 1 = DEV
static uint32_t sleepIntervalSeconds;
static uint8_t batchTarget;
static uint8_t currentIntervalIndex; // 0-10; from setup() and after successful uplink (temperature/battery algorithm)
static uint8_t currentFPort;
// The active power policy. Chosen at boot by probing for the INA219.
static PrimaryCellPolicy primaryPolicy;
static SolarPolicy solarPolicy;
static PowerPolicy *policy = &primaryPolicy;
static PowerVariant powerVariant = VARIANT_PRIMARY;
static Adafruit_INA219 ina219;   // only used on the solar variant

// millis() of the last sensor wake, so the solar EWMA/harvest get a real dt.
// The RTC would drift-correct better, but wake-to-wake dt only needs elapsed
// seconds, and millis() is fine awake; deep sleep advances the RTC not millis(),
// so we take dt from the interval we just slept instead (see readAndBufferSensors).
static uint32_t lastWakeMillis = 0;

// State that survives NVIC_SystemReset(). NOT initialised by the C runtime --
// that is the whole point. Validity is checked in setup() (persist.h).
__attribute__((section(".noinit"))) static PersistState persist;

// Read-only view of the RTC. We NEVER call rtc.begin() on this instance:
// RTCZero::begin(false) calls RTCreset(), which clears the counter unless
// PM->RCAUSE flags a system/watchdog/external reset -- so a begin() during
// normal operation would WIPE the time. ArduinoLowPower owns configuration
// (forced once in setup via attachInterruptWakeup); getEpoch()/setEpoch() only
// touch the clock register and do not test the instance's _configured flag.
// See docs/dev-notes for the ownership seam (S03-10).
RTCZero rtc;

// Set true when we want a network-time reply on the next uplink (S03-11).
static bool deviceTimeReqPending = false;

// The season-driving water temperature, from the 0.5-1 m sensor.
// NOT "the temperature": v4 adds air, box and depth sensors, and only THIS one
// drives the season. See docs/multi-sensor-v4-analysis.md.
static float surfaceTempC;

// True when A2 carries more than one DS18B20, so index 0 cannot be attributed
// to the surface sensor. Set once in setup(); see the guard there.
static bool sensorBusAmbiguous = false;
static volatile bool txComplete = false;
static UplinkSchedule uplinkSched; // when to send, and the 4-bit uplink counter
static uint32_t joinAttemptStart = 0;
static bool joinSuccessBlinkPending = false;

// Batch buffer: newest at index 0. Each entry 2 bytes (int16)
static uint16_t dataBuffer[MAX_BATCH];

// 1. Define the smart loggers
template<typename T>
void logPrint(T msg) {
  if (runMode == 1) Serial.print(msg);
}

template<typename T>
void logPrintln(T msg) {
  if (runMode == 1) Serial.println(msg);
}

// 2. Specialized version for F() macro (Flash strings)
void logPrint(const __FlashStringHelper *msg) {
  if (runMode == 1) Serial.print(msg);
}

void logPrintln(const __FlashStringHelper *msg) {
  if (runMode == 1) Serial.println(msg);
}

// Pin mapping for Adafruit Feather M0 LoRa
const lmic_pinmap lmic_pins = {
    .nss = 8,
    .rxtx = LMIC_UNUSED_PIN,
    .rst = 4,
    .dio = {3, 6, LMIC_UNUSED_PIN},
};

// One physical INA219 probe attempt over Wire. The DECISION logic (how many
// attempts, what a result means) is host-tested in variant_probe.h; this only
// does the I2C.
static ProbeResult probeIna219Once() {
  ProbeResult r = {false, false, 0};

  Wire.beginTransmission(INA219_I2C_ADDR);
  r.addressAcked = (Wire.endTransmission() == 0);
  if (!r.addressAcked) return r;

  // Read the config register (0x00) and check it against the reset default.
  Wire.beginTransmission(INA219_I2C_ADDR);
  Wire.write(INA219_REG_CONFIG);
  if (Wire.endTransmission(false) != 0) return r;   // repeated-start failed -> hung bus
  if (Wire.requestFrom(INA219_I2C_ADDR, 2) != 2) return r;
  uint16_t hi = Wire.read();
  uint16_t lo = Wire.read();
  r.configValue = (hi << 8) | lo;
  r.configRead = true;
  return r;
}

// Probe with retries and decide the variant. A boot is rare, so 3 attempts x
// 50 ms is nothing against the delay(5000) we already spend on the radio -- and
// one transient glitch must not decide a whole session (variant_probe.h).
static PowerVariant probeVariant() {
  ProbeResult attempts[PROBE_ATTEMPTS];
  for (uint8_t i = 0; i < PROBE_ATTEMPTS; i++) {
    attempts[i] = probeIna219Once();
    if (probeAttemptFoundIna219(&attempts[i])) break;  // found -> no need to retry
    if (i + 1 < PROBE_ATTEMPTS) delay(PROBE_RETRY_MS);
  }
  return probeDecide(attempts, PROBE_ATTEMPTS);
}

// DeviceTimeReq reply. Called by LMIC after the RX window (S03-12).
// The callback carries only success/fail; the time is fetched separately.
static void onNetworkTime(void *pUserData, int flagSuccess) {
  (void)pUserData;
  if (!flagSuccess) return;   // no reply this time; stay degraded, try again later

  lmic_time_reference_t ref;
  if (!LMIC_getNetworkTimeReference(&ref)) return;

  // ref.tNetwork is GPS seconds at ref.tLocal (an ostime_t). Compensate for the
  // time elapsed since that sample, or the clock lands seconds slow.
  uint32_t elapsedMs = osticks2ms(os_getTime() - ref.tLocal);
  uint32_t utc = gpsToUnixUtc((uint32_t)ref.tNetwork, elapsedMs);

  if (!utcPlausible(utc)) return;   // a reply that did not really land

  rtc.setEpoch(utc);
  persist.clockValid = 1;
  deviceTimeReqPending = false;
}

void onEvent(ev_t ev) {
  logPrint(os_getTime());
  logPrint(": ");
  switch (ev) {
  case EV_JOINING:
    logPrintln(F("EV_JOINING"));
    break;
  case EV_JOINED:
    logPrintln(F("EV_JOINED"));
    LMIC_setLinkCheckMode(0);
    LMIC_setDrTxpow(5, 14);
    joinSuccessBlinkPending = true; // Blink in loop() to avoid delay() inside LMIC callback
    uplinkScheduleOnJoin(&uplinkSched); // arm the one-shot post-join flush
    // Ask for network time on the next uplink, unless we already have a valid
    // clock preserved across a soft reset. One acquisition holds for months at
    // this crystal's ~4 s/day, so this is a one-shot, not a standing dependency.
    if (!persist.clockValid) {
      deviceTimeReqPending = true;
    }
    break;
  case EV_JOIN_FAILED:
    logPrintln(F("EV_JOIN_FAILED"));
    break;
  case EV_TXCOMPLETE:
    logPrintln(F("EV_TXCOMPLETE (includes waiting for RX windows)"));
    if (LMIC.txrxFlags & TXRX_ACK)
      logPrintln(F("Received ack"));
    txComplete = true; // Breaks the synchronous wait loop
    break;
  case 17: // EV_TXSTART
    logPrintln(F("EV_TXSTART"));
    break;
  case 20: // EV_JOIN_TXCOMPLETE
    logPrintln(F("EV_JOIN_TXCOMPLETE"));
    break;
  default:
    logPrint(F("Unknown event "));
    logPrintln((int)ev);
    break;
  }
}

// DS18B20 conversion time at 12-bit resolution.
#define DALLAS_CONV_MS 750

void readAndBufferSensors() {
  // Measure the conversion window from requestTemperatures(), NOT from the end
  // of whatever we do inside it. The solar policy will read the INA219 in here
  // (~68 ms of averaging); that must shrink the remaining wait, not extend the
  // wake to 818 ms.
  uint32_t convStart = millis();
  sensors.requestTemperatures();

  // The policy borrows part of the conversion window (SolarPolicy: ~68 ms of
  // INA219 averaging). Because the wait below is measured from convStart, this
  // SHRINKS the remaining delay rather than extending the wake.
  policy->onWake();
  if (powerVariant == VARIANT_SOLAR) {
    // Bus voltage is measured load-side, so a full pack in bright sun (charger
    // terminated) reads panel Voc rather than ~0 -- which is why the signal keys
    // on voltage, not current. dt is the interval we just slept: millis() does
    // not advance through deep sleep, and it is the elapsed time that the EWMA
    // needs, not a wall-clock instant.
#ifdef SOLAR_NO_INA219
    // Bus voltage from the divider; no shunt, so current (harvest) is 0.
    uint16_t adc = analogRead(PANEL_ADC_PIN);
    uint16_t busMv = (uint16_t)(adc * (3.3f / 1024.0f) * PANEL_DIV_RATIO * 1000.0f);
    solarPolicy.ingestSample(busMv, 0.0f, sleepIntervalSeconds);
#else
    uint16_t busMv = (uint16_t)(ina219.getBusVoltage_V() * 1000.0f);
    float currentMa = ina219.getCurrent_mA();
    if (currentMa < 0) currentMa = 0;   // reverse leakage blocked by the Schottky
    solarPolicy.ingestSample(busMv, currentMa, sleepIntervalSeconds);
    ina219.powerSave(true);   // ~15 uA between reads (S04-03)
#endif
  }

  if (runMode == 0) {
    // PROD: plain delay for the remainder of the conversion window.
    //
    // NOT LowPower.idle(). ArduinoLowPower's alarm has ONE-SECOND granularity
    // -- setAlarmIn() does `rtc.setAlarmEpoch(now + millis/1000)`, so idle(750)
    // truncates to a zero-second alarm and returns immediately. The DS18B20 has
    // not finished converting, and getTempCByIndex() then returns the PREVIOUS
    // conversion: every reading lagged one wake interval. That was aad7bca
    // (2026-03-09), a power optimisation that saved nothing measurable --
    // quiescent draw dominates -- and silently corrupted PROD data for four
    // months. This is that commit reverted.
    //
    // delay() is safe HERE SPECIFICALLY: the radio is idle during sensor
    // conversion, so the no-delay()-near-the-radio rule does not apply. Do not
    // "optimise" this back.
    uint32_t elapsed = millis() - convStart;
    if (elapsed < DALLAS_CONV_MS) {
      delay(DALLAS_CONV_MS - elapsed);
    }
  } else {
    // DEV: spend the window servicing LMIC so USB and the MAC layer stay alive.
    // (This path was never affected by the idle() defect -- which is exactly why
    // bench testing could never have found it.)
    while (millis() - convStart < DALLAS_CONV_MS) {
      os_runloop_once();
      delay(1); // Small delay to prevent watchdog resets
    }
  }

  // An ambiguous bus reports NaN, which encodeWaterTemperature maps to a null
  // slot and seasonUpdate ignores -- so the season holds rather than drifting on
  // a reading from the wrong sensor.
  float tempC = sensorBusAmbiguous ? NAN : sensors.getTempCByIndex(0);
  surfaceTempC = tempC;
  int16_t encodedTemp = encodeWaterTemperature(tempC);

  // Shift right by 1; only indices 1..5 (never 6) to avoid buffer overrun
  for (int i = 5; i > 0; i--) {
    dataBuffer[i] = dataBuffer[i - 1];
  }
  dataBuffer[0] = (uint16_t)encodedTemp;

  uplinkScheduleOnSample(&uplinkSched);

  // Explicitly log the reading and buffer status
  logPrint(F("--> Measured surface temp: "));
  logPrint(tempC);
  logPrint(F(" C. Buffer: "));
  logPrint(uplinkSched.ramCount);
  logPrint(F("/"));
  logPrintln(uplinkSched.batchTarget);
}

void transmitBatchAndWait() {
  if (LMIC.opmode & OP_TXRXPEND) {
    logPrintln(F("OP_TXRXPEND, skip send this cycle"));
    return;
  }

  // Core owns bytes 0-8; the policy appends after that (0 for primary, 6 for
  // solar). Layout and encoding live in payload.h.
  static uint8_t payload[PAYLOAD_CORE_LEN + 8];
  uint32_t vbat_mv = (uint32_t)(VBAT_VOLTS() * 1000.0f);
  uint8_t len = payloadBuildCore(payload,
                                 currentIntervalIndex,
                                 vbat_mv,
                                 uplinkScheduleCounterForPayload(&uplinkSched),
                                 dataBuffer,
                                 uplinkSched.ramCount);
  len += policy->appendPayload(payload + len);

  if (runMode == 1) {
    digitalWrite(LED_PIN, HIGH);
  }
  // Populate the solar status byte inputs before the payload is built.
  if (powerVariant == VARIANT_SOLAR) {
    solarPolicy.bootCounter_ = persist.bootCounter;
    uint8_t flags = 0;
    if (persist.clockValid) flags |= STATUS_CLOCK_VALID;
    if (persist.bootCounter <= 1) flags |= STATUS_COLD_BOOT;   // first boots
    solarPolicy.statusFlags_ = flags;
  }

  // Piggyback a network-time request on this uplink if one is pending. It only
  // arrives in an RX window after an uplink, and may not land -- onNetworkTime
  // leaves clockValid=0 in that case and we simply ask again next time.
  if (deviceTimeReqPending) {
    LMIC_requestNetworkTime(onNetworkTime, nullptr);
  }
  txComplete = false;
  LMIC_setTxData2(currentFPort, payload, len, 0);

  // Blocking wait for EV_TXCOMPLETE (with 2-minute safety timeout)
  uint32_t waitStart = millis();
  while (!txComplete && (millis() - waitStart < 120000UL)) {
    os_runloop_once();
  }

  // If we broke out due to timeout, clear the pending TX job to prevent a hung state
  if (!txComplete) {
    logPrintln(F("FATAL: TX Timeout. Clearing pending TX data."));
    LMIC_clrTxData();
  }

  if (runMode == 1) {
    digitalWrite(LED_PIN, LOW);
  }
  if (txComplete) {
    // The ONLY place the interval changes -- which is what makes byte 0 mean
    // "the interval these six samples were taken at".
    float vbat_volts = (float)vbat_mv / 1000.0f;
    currentIntervalIndex = policy->decideInterval(surfaceTempC, vbat_volts);
    sleepIntervalSeconds = kIntervalSecondsByIndex[currentIntervalIndex];
    // Advances the uplink counter, clears the batch, and disarms the post-join
    // flush. NOT called on timeout: ramCount and the counter are both preserved,
    // so the retry carries the same counter value and the backend can tell a
    // retry from a fresh uplink.
    uplinkScheduleOnTxSuccess(&uplinkSched);

    // Snapshot everything that must survive a reset (S03-05). Read from the
    // ACTIVE policy -- a solar unit's season/voltage live on solarPolicy, and
    // reading primaryPolicy here would persist stale values.
    if (powerVariant == VARIANT_SOLAR) {
      persist.seasonState = solarPolicy.seasonState_;
      persist.voltageState = solarPolicy.voltageState_;
    } else {
      persist.seasonState = primaryPolicy.seasonState_;
      persist.voltageState = primaryPolicy.voltageState_;
    }
    persist.currentIntervalIndex = currentIntervalIndex;
    persist.uplinkCounter = uplinkSched.uplinkCounter;
    persist.surfaceTempC = surfaceTempC;
    if (persist.clockValid) persist.rtcEpoch = rtc.getEpoch();
    if (powerVariant == VARIANT_SOLAR) {
      persist.sunEwma = solarPolicy.ewma_;
      persist.harvestMilliAmpHours = solarPolicy.harvest_.totalMah;
    }
    persistSeal(&persist);
  }
}

void setup() {
  // 1. Hardware stabilize
  delay(5000);
  SPI.begin();

  // 2. Determine Power & Mode
  // We use Pin 11. If it's GND, it's DEV; if floating, it's PROD.
  pinMode(STRAP_PIN, INPUT_PULLUP);
  delay(100); // Longer delay to ensure pull-up is stable

  // Probe for the INA219 and select the power variant BEFORE touching the policy.
  Wire.begin();
  powerVariant = probeVariant();
#ifdef SOLAR_NO_INA219
  // Bench mode: no INA219 to probe, so force solar and read the panel via ADC.
  powerVariant = VARIANT_SOLAR;
  analogReadResolution(10);   // match the 1024-step math below
#endif
  policy = (powerVariant == VARIANT_SOLAR)
             ? (PowerPolicy *)&solarPolicy
             : (PowerPolicy *)&primaryPolicy;
  if (powerVariant == VARIANT_SOLAR) {
#ifndef SOLAR_NO_INA219
    // 16 V / 400 mA calibration -> 0.1 mA/LSB. The breakout's 32 V/2 A default
    // gives 0.8 mA/LSB, ~4% resolution against a 30 mA panel (S04-01).
    ina219.begin();
    ina219.setCalibration_16V_400mA();
#endif
  }

  // Restore state across a soft reset, or cold-boot if it is not ours / not
  // this layout / corrupt (persist.h). bootCounter increments either way.
  bool coldBoot = !persistValid(&persist);
  if (coldBoot) {
    persistInit(&persist);
  }
  persist.bootCounter = (uint8_t)((persist.bootCounter + 1) & 0x07); // 3-bit
  persistSeal(&persist);

  currentIntervalIndex = 2; // 5 min initial; then from the policy after each successful TX
  policy->begin();          // starts at Summer; hysteresis settles it from real readings
  // Restore the season-driving inputs the policy just reset, if we have them.
  if (!coldBoot) {
    primaryPolicy.seasonState_ = persist.seasonState;
    primaryPolicy.voltageState_ = persist.voltageState;
    solarPolicy.seasonState_ = persist.seasonState;
    solarPolicy.voltageState_ = persist.voltageState;
    solarPolicy.ewma_ = persist.sunEwma;
    solarPolicy.harvest_.totalMah = persist.harvestMilliAmpHours;
    // Re-derive the latched bonus from the restored EWMA (conservative: off until
    // it re-engages), so a reset cannot leave the bonus stuck on.
    solarPolicy.bonusActive_ = false;
    currentIntervalIndex = persist.currentIntervalIndex ? persist.currentIntervalIndex : 2;
    surfaceTempC = persist.surfaceTempC;
  } else {
    surfaceTempC = 15.0f;   // safe default until the first reading
  }
  uint8_t intervalIdx = (currentIntervalIndex > 10) ? 10 : currentIntervalIndex;
  sleepIntervalSeconds = kIntervalSecondsByIndex[intervalIdx];
  batchTarget = 6;
  uplinkScheduleInit(&uplinkSched, batchTarget);
  uplinkSched.uplinkCounter = persist.uplinkCounter; // preserve across soft reset

  // Read the strap: only runMode differs (USB/Serial, join timeout, sleep path still vary by mode)
  // LOW = Connected to GND (Development)
  // HIGH = Floating (Production)
  if (digitalRead(STRAP_PIN) == LOW) {
    runMode = 1; // DEV
  } else {
    runMode = 0; // PROD
  }
  currentFPort = policy->fport(runMode); // 10/20 primary, 11/21 solar

  // 3. Start feedback: LED on 1s (both modes)
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);
  delay(1000);
  digitalWrite(LED_PIN, LOW);

  // 4. USB Management
  if (runMode == 0) {
    // Kill USB for real power savings
    #if defined(USBCON)
      USBDevice.detach();
    #endif
  } else {
    Serial.begin(9600);
    uint32_t start = millis();
    while (!Serial && millis() - start < 3000);
  }

  // 5. Peripherals & LMIC
  sensors.begin();

  // v4 GUARD: more than one device on this bus means we cannot attribute a
  // reading to the surface sensor.
  //
  // getTempCByIndex(0) returns devices in ROM-ADDRESS SORT ORDER -- an arbitrary
  // property of the silicon. The day someone wires a second DS18B20 to A2 to try
  // it out, the reading silently becomes a DIFFERENT sensor, with no error and no
  // symptom except wrong data. v4 adds box/air/depth using PIN-PER-ROLE precisely
  // so this cannot happen; this guard covers the interim.
  // See docs/multi-sensor-v4-analysis.md.
  //
  // NOTE what this deliberately does NOT do: it does not stop the device.
  //
  //   count == 0  -> a dead or unplugged sensor. Keep running. getTempCByIndex
  //                  returns -127, which encodeWaterTemperature already maps to
  //                  a null slot, so the backend sees "alive but blind" AND
  //                  keeps getting battery telemetry. Halting here would turn a
  //                  failed sensor into a silent decommission -- strictly worse.
  //   count  > 1  -> ambiguous. Report nulls rather than a reading we cannot
  //                  attribute, but keep uplinking. Loud in the data, not dark.
  //
  // Reporting nothing is recoverable. Reporting the wrong sensor's water
  // temperature as if it were the surface is not.
  sensorBusAmbiguous = (sensors.getDeviceCount() > 1);
  if (sensorBusAmbiguous) {
    logPrint(F("ERROR: more than one DS18B20 on A2, found "));
    logPrintln(sensors.getDeviceCount());
    logPrintln(F("Temperatures will report as null until the bus has exactly one device."));
  }
  // Force ArduinoLowPower to configure the RTC exactly once, via its own API,
  // so getEpoch() is valid before the first sleep (the first uplink after join
  // happens before any deepSleep). We never call rtc.begin() ourselves -- see
  // the RTCZero global comment and S03-10.
  LowPower.attachInterruptWakeup(RTC_ALARM_WAKEUP, nullptr, (irq_mode)0);
  // The RTC does not survive NVIC_SystemReset() (no backup domain on SAMD21), so
  // a valid clock is restored from .noinit, where it was stashed before the
  // join-failure reset (S03-06).
  if (!coldBoot && persist.clockValid && utcPlausible(persist.rtcEpoch)) {
    rtc.setEpoch(persist.rtcEpoch);
  } else {
    persist.clockValid = 0;   // cold boot loses the clock
  }

  os_init();
  LMIC_reset();
  LMIC_setClockError((uint32_t)MAX_CLOCK_ERROR * 5 / 100);
  LMIC_startJoining();
  joinAttemptStart = millis();
}

void loop() {
  // STATE 1: Commissioning (Join logic)
  if (LMIC.devaddr == 0) {
    // 1. If this is the start of a join attempt, mark the time
    if (joinAttemptStart == 0) joinAttemptStart = millis();

    os_runloop_once();

    // 2. Check: Has it been more than 3 minutes SINCE we started this attempt?
    if (runMode == 0 && (millis() - joinAttemptStart > 180000UL)) {
      joinAttemptStart = 0; // Reset for next time
      LowPower.deepSleep(900000);
      // The RTC counted those 15 minutes; stash the CURRENT epoch (already
      // includes them) so the clock survives the reset. Do NOT add the sleep
      // back on restore -- it is already in the value we read here (S03-06).
      if (persist.clockValid) {
        persist.rtcEpoch = rtc.getEpoch();
        persistSeal(&persist);
      }
      NVIC_SystemReset(); // Start fresh
    }
    return;
  }

  // STATE 2: Operational
  if (joinSuccessBlinkPending) {
    joinSuccessBlinkPending = false;
    // Post-join blink: 50 ms waits run os_runloop_once() so MAC commands from the network can be processed.
    for (int i = 0; i < 5; i++) {
      digitalWrite(LED_PIN, HIGH);
      uint32_t t = millis();
      while (millis() - t < 50) {
        os_runloop_once();
        delay(1); // Small delay to prevent watchdog resets
      }
      digitalWrite(LED_PIN, LOW);
      t = millis();
      while (millis() - t < 50) {
        os_runloop_once();
        delay(1); // Small delay to prevent watchdog resets
      }
    }
  }
  readAndBufferSensors();

  // Send on the first uplink after joining, or when the batch is full.
  //
  // This used to read `wakeCounter == 1`, which was NOT "first after join":
  // wakeCounter is 4-bit, so it wrapped to 1 every 16 wakes and re-fired with a
  // partial batch. A third of every uplink ever sent carried 4 samples and two
  // dead bytes. The flag is explicit now; never infer "first" from a counter
  // that wraps.
  if (uplinkScheduleShouldSend(&uplinkSched)) {
    logPrintln(F("*** TRIGGERING UPLINK (first after join, or batch full) ***"));
    transmitBatchAndWait();
  }

  // STATE 3: Sleep
  logPrint(F("Entering sleep for "));
  logPrint(sleepIntervalSeconds);
  logPrintln(F(" seconds..."));

  // Ensure serial finishes printing before sleep/delay
  if (runMode == 1) Serial.flush();

  if (runMode == 1) {
    // DEV MODE: Keep the LMIC stack running while simulating a sleep period
    uint32_t waitStart = millis();
    while (millis() - waitStart < (sleepIntervalSeconds * 1000UL)) {
      os_runloop_once();
      delay(1); // Small delay to prevent watchdog resets
    }
  } else {
    // PROD MODE: True hardware deep sleep.
    LowPower.deepSleep(sleepIntervalSeconds * 1000UL);
  }

  logPrintln(F("Woke up!"));
}