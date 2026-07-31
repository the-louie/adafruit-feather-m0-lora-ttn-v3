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
#include "keygen.h"
#include "keygen_salt.h"
#include "diagnostics.h"
#include "sensor_plausibility.h"
#include "ina219_bus.h"
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
// TTN OTAA CREDENTIALS -- derived on boot from the SAMD21 silicon serial.
// -----------------------------------------------------------------------------
// One universal binary: each board reconstructs its own DevEUI + AppKey from its
// permanent 128-bit serial mixed with the shared secret salt (keygen.h +
// keygen_salt.h), so there are no per-board key edits and no way to flash the
// wrong keys. In DEV the derived keys print once at boot for TTN registration.
//
// The JoinEUI (AppEUI) is NOT derived -- it is a fixed fleet-wide constant.
//   JoinEUI = 0x0000000000000001  (LMIC wants it little-endian, LSB first)
static const u1_t PROGMEM APPEUI[8] = {0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
void os_getArtEui(u1_t *buf) { memcpy_P(buf, APPEUI, 8); }

// Populated once in setup() by deriveBoardCredentials(), before LMIC joins.
static DerivedCreds g_creds;

// Read the SAMD21's 128-bit factory serial (four words at these fixed addresses,
// most-significant word first) into 16 bytes.
static void readChipSerial(uint8_t out[16]) {
  const uint32_t addr[4] = {0x0080A00C, 0x0080A040, 0x0080A044, 0x0080A048};
  for (int w = 0; w < 4; ++w) {
    uint32_t word = *(const volatile uint32_t *)addr[w];
    out[w * 4 + 0] = (uint8_t)(word >> 24);
    out[w * 4 + 1] = (uint8_t)(word >> 16);
    out[w * 4 + 2] = (uint8_t)(word >> 8);
    out[w * 4 + 3] = (uint8_t)(word);
  }
}

// Derive this board's credentials into g_creds. Call once, before LMIC joins.
static void deriveBoardCredentials() {
  uint8_t serial[16];
  readChipSerial(serial);
  deriveCredentials(serial, KEYGEN_SALT, KEYGEN_SALT_LEN, g_creds);
}

// Legacy-device key override, compiled ONLY when FW_FIXED_KEYS is defined
// (scripts/build.sh --fixed-keys). Devices provisioned before credentials
// became derived from the silicon serial hold a TTN-issued DevEUI that this
// firmware would otherwise replace with a derived one TTN has never seen --
// the join would be rejected and the unit would go silent. Pinning the
// original identity keeps such a device on its existing registration,
// decoder and backend path. See fixed_keys.h (gitignored, holds a root key).
#ifdef FW_FIXED_KEYS
#include "fixed_keys.h"
#endif

// LMIC wants the DevEUI little-endian; the canonical form is MSB-first.
void os_getDevEui(u1_t *buf) {
#ifdef FW_FIXED_KEYS
  euiToLmicLE(FIXED_DEVEUI_MSB, buf);
#else
  euiToLmicLE(g_creds.devEui, buf);
#endif
}
// AppKey is MSB-first for LMIC -- same as the canonical derived form.
void os_getDevKey(u1_t *buf) {
#ifdef FW_FIXED_KEYS
  memcpy(buf, FIXED_APPKEY, 16);
#else
  memcpy(buf, g_creds.appKey, 16);
#endif
}

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

// The solar EWMA/harvest dt is the interval we just slept (readAndBufferSensors),
// NOT wall-clock time: millis() does not advance through deep sleep, and elapsed
// seconds is all the EWMA needs.
//
// True until the first sensor read of this boot has been taken. That first read
// has NOT slept: setup() fills sleepIntervalSeconds from the restored interval
// index before any sleep happens, so passing it as dt would credit elapsed time
// that never elapsed. See readAndBufferSensors().
static bool firstSampleAfterBoot = true;

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

// --- Diagnostics (diagnostics.h): a separate error/health uplink on its own
// FPort, so a field unit with no USB can still report faults. ---
#define DIAG_FPORT_PROD 1
#define DIAG_FPORT_DEV  2
#define DIAG_MIN_RESEND_SECONDS 86400UL   // re-alert a persistent fault at most daily

static uint8_t  g_resetCause = 0;      // PM->RCAUSE, latched at boot
static bool     g_coldBoot = false;    // persist was not restored this boot
static bool     g_persistCorrupt = false; // .noinit looked ours but the CRC failed
static uint8_t  g_ds18Count = 0;       // OneWire device count, cached in setup
static bool     g_ina219Present = false;  // the boot probe found the INA219
static uint16_t g_probeConfig = 0;     // INA219 config register read during the probe
static bool     g_ina219ReadOk = true; // solar: last live INA219 read passed the
                                       // ina219_bus.h verdict (I2C + CNVR + sanity)
static bool     g_ina219Ovf = false;   // solar: OVF math-overflow flag on the last
                                       // live read -- current/power meaningless
// An uplink failed (TX timeout, or the stack refused to queue) and no diagnostic
// frame has reported it yet. A LATCH, not a last-attempt flag: the overnight
// 2026-07-27/28 stall proved a plain flag can never reach the air -- the fault
// frame that would carry it is dropped by the same failure, and the next
// successful data uplink then cleared the flag before the next diagnostic
// evaluation. Cleared ONLY after a diagnostic frame actually transmits.
static bool     g_txFaultPending = false;
static bool     bootDiagSent = false;  // has the once-per-boot diagnostic frame gone out?
// Sensor-diagnostics state (TODO items 27-31).
static uint8_t  g_ds18Status = DS18_NOT_FOUND; // Ds18Status of the last wake's read
static bool     g_tempImplausible = false;     // valid reading, impossible water step
static uint8_t  g_ds18Rom[3] = {0, 0, 0};      // last sensor SEEN this boot (ds18CaptureRom)
static float    g_prevTempC = NAN;             // last VALID reading, for the step check

// Verbose DEV diagnostics (diagnostics.h) -- a full-state snapshot on its own
// FPort, DEV-only, on a fixed cadence. DEV never deep-sleeps, so millis() advances
// and gates the cadence directly. For a short planned outage you may want finer
// resolution than hourly -- just lower VERBOSE_INTERVAL_MS.
// Firmware identity for the verbose frame (schema 3, bytes 34-36): the first
// 6 hex chars of the git commit this binary was built from, injected by
// scripts/build.sh via -DFW_GIT_HASH24=0x<hash>. A build made any other way
// gets 0x000000, which the decoder reports as null -- an "unofficial build"
// marker, so a bench-flashed ad-hoc image can never masquerade as a release.
// The hash lives only in the binary, never in a committed file: a committed
// file cannot contain the hash of the commit that contains it.
#ifndef FW_GIT_HASH24
#define FW_GIT_HASH24 0x000000UL
#endif

#define VERBOSE_FPORT_DEV   3
#define VERBOSE_INTERVAL_MS 3600000UL   // ~1 hour
static uint32_t lastVerboseMillis = 0;
static bool     verboseSentOnce = false;

// Schema 2 (item 25): the frame now carries uptime, a wake-cycle count and a
// panel min/mean/max profile, and is emitted from INSIDE the DEV sleep loop as
// well as at wake -- so the cadence is genuinely ~hourly instead of
// max(1 h, wake interval), which used to thin the status exactly when long
// winter/degraded intervals made it most wanted. All DEV-only; PROD never
// emits the frame and never runs the sampler.
#define PANEL_STATS_SAMPLE_MS 60000UL   // one profile sample per minute
static uint32_t   lastPanelSampleMillis = 0;
static PanelStats g_panelStats;          // reset after each verbose TX
static uint16_t   g_cycleCount = 0;      // wake cycles (sensor reads) since boot

// Spacing between verbose ATTEMPTS. The due-time (lastVerboseMillis) advances
// only on success, and the sleep loop evaluates every iteration, so a failed
// attempt would otherwise retry immediately, each retry blocking for up to
// TX_READY_WAIT_MS. Five minutes keeps a wedged stack at ~2% blocking duty
// while still retrying far sooner than the next wake would.
#define VERBOSE_RETRY_BACKOFF_MS 300000UL
static bool     verboseAttemptMade = false;
static uint32_t lastVerboseAttemptMillis = 0;

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
  ProbeResult r = {false, false, 0, false, 0};

  // Soft-reset the INA219 BEFORE reading its config. On a WARM MCU reset (RST
  // button, watchdog, the PROD join-failure NVIC_SystemReset) the INA219 stays
  // powered and still holds the config we wrote last boot -- 0x019F from
  // setCalibration_16V_400mA -- which is NOT the power-on default 0x399F this
  // probe recognises. Without this reset the sensor reads as absent and a solar
  // unit boots the PRIMARY policy (the A1 misdetect; seen on gisebo-05 2026-07-27).
  // Writing RST (config bit 15) returns the register to 0x399F. A NAK here
  // (nothing at 0x40) is harmless; setCalibration re-configures it later.
  Wire.beginTransmission(INA219_I2C_ADDR);
  Wire.write(INA219_REG_CONFIG);
  Wire.write((uint8_t)0x80);   // config MSB: RST bit (0x8000), MSB-first
  Wire.write((uint8_t)0x00);   // config LSB
  Wire.endTransmission();
  delay(1);                    // INA219 reset completes in < 1 ms (datasheet)

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

  // Second identity word: after the RST above, a real INA219's Calibration
  // register (05h) MUST read 0x0000 ("resets all registers to default values").
  // Two extra I2C bytes turn the 16-bit config match into a 32-bit identity --
  // the part has no ID register, so this is all the identification it offers.
  Wire.beginTransmission(INA219_I2C_ADDR);
  Wire.write(INA219_REG_CALIBRATION);
  if (Wire.endTransmission(false) != 0) return r;
  if (Wire.requestFrom(INA219_I2C_ADDR, 2) != 2) return r;
  hi = Wire.read();
  lo = Wire.read();
  r.calValue = (hi << 8) | lo;
  r.calRead = true;
  return r;
}

// Probe with retries and decide the variant. A boot is rare, so 3 attempts x
// 50 ms is nothing against the delay(5000) we already spend on the radio -- and
// one transient glitch must not decide a whole session (variant_probe.h).
static PowerVariant probeVariant() {
  ProbeResult attempts[PROBE_ATTEMPTS];
  for (uint8_t i = 0; i < PROBE_ATTEMPTS; i++) {
    attempts[i] = probeIna219Once();
    g_probeConfig = attempts[i].configValue;  // config from the last attempt tried (diagnostics)
    if (probeAttemptFoundIna219(&attempts[i])) break;  // found -> no need to retry
    if (i + 1 < PROBE_ATTEMPTS) delay(PROBE_RETRY_MS);
  }
  PowerVariant v = probeDecide(attempts, PROBE_ATTEMPTS);
  g_ina219Present = (v == VARIANT_SOLAR);
  return v;
}

// Raw 2-byte read of the INA219 bus-voltage register (02h). The Adafruit
// accessor discards bits 2..0 -- and bits 1/0 are CNVR and OVF, the only status
// the part has. Same Wire mechanics as the probe; the interpretation of the
// value is host-tested in ina219_bus.h.
static bool ina219ReadBusRaw(uint16_t *out) {
  Wire.beginTransmission(INA219_I2C_ADDR);
  Wire.write(INA219_REG_BUS_ADDR);
  if (Wire.endTransmission(false) != 0) return false;   // repeated start
  if (Wire.requestFrom(INA219_I2C_ADDR, 2) != 2) return false;
  uint16_t hi = Wire.read();
  uint16_t lo = Wire.read();
  *out = (uint16_t)((hi << 8) | lo);
  return true;
}

// Wake the INA219 out of power-down and wait for a conversion that completed
// AFTER the wake. Returns true when CNVR is seen; the raw bus register and the
// I2C health of the final read come back through the out-parameters.
//
// The wake writes MODE=111, which clears CNVR (a mode write, and not to the
// excepted power-down/disable values), so a set CNVR below proves a fresh
// conversion: a blind delay cannot distinguish "converted" from "served the
// same stale registers", and a powered-down part happily ACKs stale reads
// forever. Polling reads register 02h, which does not self-clear (only a
// Power-register read does, and this firmware never reads 03h). Worst-case
// legitimate conversion is 1.17 ms against the 10 ms budget; if the ADC
// config ever changes to averaging, a too-short budget FAULTS LOUDLY on every
// wake instead of silently serving stale data. See ina219_bus.h.
//
// The caller decides what a failure means (fault the cycle, or skip a profile
// sample) and returns the part to power-down with powerSave(true).
static bool ina219WakeForReading(uint16_t *rawOut, bool *busReadOkOut) {
  ina219.powerSave(false);
  uint16_t raw = 0;
  bool busReadOk = false, convReady = false;
  uint32_t t0 = millis();
  do {
    busReadOk = ina219ReadBusRaw(&raw);
    convReady = busReadOk && ina219BusConversionReady(raw);
    if (convReady) break;
    os_runloop_once();
  } while (millis() - t0 < INA219_CNVR_TIMEOUT_MS);
  *rawOut = raw;
  *busReadOkOut = busReadOk;
  return convReady;
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

// Wait out the remainder of a conversion window measured from startMs.
//
// PROD: plain delay. NOT LowPower.idle() -- ArduinoLowPower's alarm has
// ONE-SECOND granularity (setAlarmIn() does `rtc.setAlarmEpoch(now +
// millis/1000)`), so idle(750) truncates to a zero-second alarm and returns
// immediately. The DS18B20 has not finished converting, and getTempCByIndex()
// then returns the PREVIOUS conversion: every reading lagged one wake interval.
// That was aad7bca (2026-03-09), a power optimisation that saved nothing
// measurable -- quiescent draw dominates -- and silently corrupted PROD data
// for four months. delay() is safe HERE SPECIFICALLY: the radio is idle during
// sensor conversion, so the no-delay()-near-the-radio rule does not apply. Do
// not "optimise" this back.
//
// DEV: spend the window servicing LMIC so USB and the MAC layer stay alive.
// (This path was never affected by the idle() defect -- which is exactly why
// bench testing could never have found it.)
static void ds18ConversionWait(uint32_t startMs) {
  if (runMode == 0) {
    uint32_t elapsed = millis() - startMs;
    if (elapsed < DALLAS_CONV_MS) {
      delay(DALLAS_CONV_MS - elapsed);
    }
  } else {
    while (millis() - startMs < DALLAS_CONV_MS) {
      os_runloop_once();
      delay(1); // Small delay to prevent watchdog resets
    }
  }
}

// Record the low 3 bytes of the ROM serial of sensor index 0 (TODO 31): enough
// to notice a swapped sensor over the air. addr[0] is the family code, addr[7]
// the CRC. On failure the previous value is kept -- the frame then names the
// LAST sensor seen, which is exactly what a not-found fault should report.
static void ds18CaptureRom() {
  DeviceAddress a;
  if (g_ds18Count >= 1 && sensors.getAddress(a, 0)) {
    g_ds18Rom[0] = a[1]; g_ds18Rom[1] = a[2]; g_ds18Rom[2] = a[3];
  }
}

void readAndBufferSensors() {
  // Measure the conversion window from requestTemperatures(), NOT from the end
  // of whatever we do inside it. The solar variant reads the INA219 in here
  // (~7 ms: wake from powerdown + conversion); that must shrink the remaining
  // wait, not extend the wake past 750 ms.
  uint32_t convStart = millis();
  sensors.requestTemperatures();

  // The policy borrows part of the conversion window (the INA219 wake + read
  // below). Because the wait below is measured from convStart, this SHRINKS the
  // remaining delay rather than extending the wake.
  policy->onWake();
  if (powerVariant == VARIANT_SOLAR) {
    // The sun predicate takes bus voltage, current AND battery voltage -- the
    // night bus back-feeds from the pack to battery - ~180 mV, so "lit" is only
    // decidable relative to the battery or by measurable charge current (either
    // arm; solar_signal.h has the measured table). dt is the interval we just
    // slept: millis() does not advance through deep sleep, and it is the
    // elapsed time that the EWMA needs, not a wall-clock instant.
    //
    // ...except on the FIRST read of a boot, which slept for nothing at all:
    // setup() fills sleepIntervalSeconds from the restored interval index before
    // any sleep happens. Passing it would fabricate an interval of elapsed time.
    // Negligible for the EWMA (alpha 0.041 once), but the harvest accumulator
    // integrates current*dt directly, so a warm reset at index 5 in sun would
    // credit ~34 mAh that never flowed -- more than a typical DAY's harvest
    // (7-28 mAh/day at balance) -- on top of the .noinit-restored total. PROD is
    // the worse case: the join-failure NVIC_SystemReset() re-enters setup() with
    // a RESTORED index, not the 5-min cold-boot default.
    //
    // Zero deliberately UNDER-counts (the panel may really have been charging
    // while we were off), because the off-duration is not knowable from millis(),
    // which does not advance through deep sleep or across a reset -- and a
    // fabricated value is indistinguishable from real harvest downstream. The
    // RTC could supply it; not taken, as that would add a clock dependency to a
    // path that must work before the clock is valid.
    // See docs/dev-notes/20260728-2000_first-sample-dt-and-tx-ready-wait.md.
    const uint32_t ingestDt = firstSampleAfterBoot ? 0 : sleepIntervalSeconds;
    // Battery reading for the sun predicate's relative arm -- sampled in the
    // same wake as the bus reading so both see the same conditions.
    const uint16_t batteryMvNow = (uint16_t)(VBAT_VOLTS() * 1000.0f);
#ifdef SOLAR_NO_INA219
    // Bus voltage from the divider; no shunt, so current (harvest) is 0.
    uint16_t adc = analogRead(PANEL_ADC_PIN);
    uint16_t busMv = (uint16_t)(adc * (3.3f / 1024.0f) * PANEL_DIV_RATIO * 1000.0f);
    solarPolicy.ingestSample(busMv, 0.0f, batteryMvNow, ingestDt);
#else
    // Wake the part and require a fresh conversion (CNVR) before trusting the
    // registers: a powered-down INA219 serves its stale contents forever, which
    // is how plausible-but-frozen panel telemetry once passed for healthy all
    // night (docs/dev-notes/20260728-1230_ina219-powersave-freeze.md). The
    // mechanics and the datasheet reasoning live in ina219WakeForReading().
    uint16_t rawBus = 0;
    bool busReadOk = false;
    bool convReady = ina219WakeForReading(&rawBus, &busReadOk);
    uint16_t busMv = ina219BusMillivolts(rawBus);
    bool ovf = busReadOk && ina219BusOverflow(rawBus);
    // success() reflects only the MOST RECENT register read, so it is sampled
    // immediately after the accessor it vouches for (the bus voltage now comes
    // from the raw CNVR-gated read above, with its own ok flag).
    float currentMa = ina219.getCurrent_mA();
    bool currentOk = ina219.success();
    if (currentMa < 0) currentMa = 0;   // reverse leakage blocked by the Schottky
    // OVF = the Current/Power CALCULATIONS are out of range ("data may be
    // meaningless"). Unreachable in our config unless something structural
    // broke -- most plausibly a corrupted Calibration register, rewritten on
    // every current read. Bus voltage is a direct ADC result and stays valid,
    // so the EWMA keeps its input; the current does not, so it must not reach
    // the harvest accumulator. Surfaced as DIAG_FAULT_INA219_OVF.
    g_ina219Ovf = ovf;
    if (ovf) currentMa = 0;
    // The verdict (ina219_bus.h): I2C ACKs + CNVR + < 20 V. Deliberately no
    // lower bound -- a dark panel legitimately reads ~0 mV all night, and that
    // IS the sun signal working.
    g_ina219ReadOk = ina219LiveReadOk(busReadOk, convReady, currentOk, busMv);
    if (g_ina219ReadOk) {
      solarPolicy.ingestSample(busMv, currentMa, batteryMvNow, ingestDt);
      // The wake-time read is also a profile point for the schema-2 stats.
      if (runMode == 1) panelStatsAdd(&g_panelStats, busMv, currentMa);
    }
    // else: do NOT ingest. Feeding unconverted/garbage values to the EWMA and
    // harvest is exactly the overnight failure this gate exists to stop; the
    // skipped interval simply is not integrated, and the fault frame reports
    // DIAG_FAULT_INA219_READ_FAIL. lastBusMv_/lastCurrentMa_ keep their prior
    // values, so the payload shows the last GOOD sample, flagged unhealthy.
    ina219.powerSave(true);   // ~15 uA between reads (S04-03)
#endif
  }

  ds18ConversionWait(convStart);

  // An ambiguous bus reports NaN, which encodeWaterTemperature maps to a null
  // slot and seasonUpdate ignores -- so the season holds rather than drifting on
  // a reading from the wrong sensor.
  float tempC = sensorBusAmbiguous ? NAN : sensors.getTempCByIndex(0);

  // One retry inside the wake (TODO 30). A single transient -- EMI, a marginal
  // CRC, a momentary contact, a sensor that browned out mid-conversion and now
  // reads its 85.00 power-on default -- otherwise costs the whole interval's
  // sample and is indistinguishable from a hard fault. The trigger is the same
  // tested derivation that later produces the status byte, so EVERY failed
  // flavour gets its one retry. The cost is a second conversion window plus a
  // bus search, paid ONLY in the failure path; a healthy wake pays nothing.
  // Failing twice inside one wake is much stronger evidence of a real fault
  // than one miss.
  if (ds18DeriveStatus(g_ds18Count, sensorBusAmbiguous, tempC) != DS18_OK) {
    // Re-enumerate on ANY failure: the count is otherwise cached from setup(),
    // and the cached topology may itself be the fault. A chain that failed open
    // after boot should report not_found (the count is now truly 0), a sensor
    // attached after boot (the bench swap-test on a broken unit) should be
    // found, and a two-sensor bus reduced to one should leave AMBIGUOUS. The
    // status byte describes the bus as it is NOW, not as setup() found it.
    // begin() re-runs the bus search.
    sensors.begin();
    g_ds18Count = sensors.getDeviceCount();
    sensorBusAmbiguous = (g_ds18Count > 1);
    if (g_ds18Count == 1) {
      sensors.requestTemperatures();
      ds18ConversionWait(millis());
      tempC = sensors.getTempCByIndex(0);
    }
  }

  // Status code (TODO 28), from the POST-retry outcome. Pure derivation, host-
  // tested; the wire carries which flavour of failure, not just that one
  // happened.
  g_ds18Status = ds18DeriveStatus(g_ds18Count, sensorBusAmbiguous, tempC);
  // A successful read refreshes the ROM identity: getTempCByIndex() re-searches
  // the bus on every call, so a sensor swapped between wakes reads fine without
  // any failure ever occurring -- only a fresh capture keeps bytes 13-15 of the
  // fault frame naming the sensor actually attached.
  if (g_ds18Status == DS18_OK) ds18CaptureRom();

  // Consecutive-failure streak (TODO 29), persisted: the daily fault rate limit
  // hides failure FREQUENCY, and a fault that spans the join-failure reset is a
  // different animal from one that does not. Counts wake-level outcomes, not
  // individual attempts.
  if (g_ds18Status == DS18_OK) {
    persist.sensorFailStreak = 0;
  } else if (persist.sensorFailStreak < 255) {
    persist.sensorFailStreak++;
  }
  persistSeal(&persist);

  // Water-step plausibility (TODO 27): a VALID reading that moved faster than
  // water physically can means the sensor is likely measuring air. Thresholds
  // derived from fleet data; see sensor_plausibility.h. No comparison on the
  // first sample after boot (dt would be fabricated -- same reasoning as the
  // ingest guard) or across an invalid gap. The previous reading is adopted
  // either way, so one excursion raises the fault once, on its leading edge,
  // rather than latching against an ever-staler baseline; a sensor left in air
  // re-alerts on each new acute swing.
  if (g_ds18Status == DS18_OK) {
    g_tempImplausible = firstSampleAfterBoot
        ? false
        : !waterStepPlausible(g_prevTempC, tempC, sleepIntervalSeconds);
    g_prevTempC = tempC;
  } else {
    g_tempImplausible = false;   // no reading, no verdict; the read fault carries it
    g_prevTempC = NAN;           // do not compare across a failed gap
  }

  surfaceTempC = tempC;
  int16_t encodedTemp = encodeWaterTemperature(tempC);

  // Shift right by 1; only indices 1..5 (never 6) to avoid buffer overrun
  for (int i = 5; i > 0; i--) {
    dataBuffer[i] = dataBuffer[i - 1];
  }
  dataBuffer[0] = (uint16_t)encodedTemp;

  uplinkScheduleOnSample(&uplinkSched);

  // Cleared here rather than inside the solar branch so it means "the first
  // sensor read of this boot", independent of variant -- a primary board must
  // not leave it armed for a policy it never runs.
  firstSampleAfterBoot = false;
  // Schema-2 wake-cycle counter: one per sensor read, saturating (a bench run
  // long enough to wrap 65535 hourly wakes is not a bench run).
  if (g_cycleCount < 65535) g_cycleCount++;

  // Explicitly log the reading and buffer status
  logPrint(F("--> Measured surface temp: "));
  logPrint(tempC);
  logPrint(F(" C. Buffer: "));
  logPrint(uplinkSched.ramCount);
  logPrint(F("/"));
  logPrintln(uplinkSched.batchTarget);
}

// How long to let LMIC finish whatever it owes the network before we give up on
// queueing a frame this cycle. The MAC ping-pong runs at the ~6 s EU868 1%
// pacing, so this covers about five exchanges -- the boot burst on gisebo-05
// was four, spread over 17 s.
#define TX_READY_WAIT_MS 30000UL

// Spin the LMIC runloop until it will accept a frame, or the budget expires.
//
// Replaces an instant `if (LMIC.opmode & OP_TXRXPEND) return;` bail. That guard
// tested ONE bit, but LMIC_setTxData2() refuses on OP_POLL | OP_TXDATA |
// OP_JOINING | OP_TXRXPEND -- and OP_POLL (LMIC owes the network a MAC answer)
// is set after every downlink. Since each of our uplinks draws a downlink, the
// second and third frames of a cycle were refused as a matter of course: on
// gisebo-05 2026-07-28 the boot cycle sent the data frame and deferred BOTH the
// fault and verbose frames, and the next cycle sent the fault frame and deferred
// verbose again. Only ever one frame per cycle got out.
//
// A delay rather than a loss -- bootDiagSent/verboseSentOnce are only set on
// success -- but in PROD the retry waits a whole sleep interval, up to 7 days at
// index 10, on the once-per-boot "I am alive, here is my reset cause" frame.
// Waiting a few seconds for the MAC exchange to drain is far cheaper than that.
//
// Uses LMIC_queryTxReady() (the library's own !LMICJ_isTxPathBusy()) rather than
// hand-testing opmode bits: picking bits by hand is exactly how the old guard
// drifted out of step with the library. See
// docs/dev-notes/20260728-2000_first-sample-dt-and-tx-ready-wait.md.
static bool waitForTxReady(const __FlashStringHelper *tag) {
  if (LMIC_queryTxReady()) return true;
  uint32_t waitStart = millis();
  while (!LMIC_queryTxReady() && (millis() - waitStart < TX_READY_WAIT_MS)) {
    os_runloop_once();
  }
  if (!LMIC_queryTxReady()) {
    logPrint(tag);
    logPrintln(F(" TX path still busy after the wait, defer to next cycle"));
    logTxSchedState(F("busy:"));
    // Budgeted for five MAC exchanges, so exhausting it is not routine
    // contention -- it means the stack is wedged. Worth a fault, unlike the
    // ordinary "wait a moment" case which now resolves inside the budget.
    g_txFaultPending = true;
    return false;
  }
  return true;
}

void transmitBatchAndWait() {
  if (!waitForTxReady(F("data uplink:"))) {
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
  // Populate the solar status byte inputs BEFORE appendPayload() reads them.
  // appendPayload() writes byte 14 from bootCounter_/statusFlags_, so if these
  // are set afterwards the status byte is stale-by-one -- zero on the very first
  // uplink (confirmed on gisebo-05's f_cnt 0: status 0x00, boot_counter 0,
  // cold_boot clear, when it should have read boot_counter 1 + cold_boot).
  // See docs/dev-notes/20260726-*_status-byte-stale-by-one.md.
  if (powerVariant == VARIANT_SOLAR) {
    solarPolicy.bootCounter_ = persist.bootCounter;
    uint8_t flags = 0;
    if (persist.clockValid) flags |= STATUS_CLOCK_VALID;
    // Cold/soft boot from g_coldBoot, NOT from `bootCounter <= 1`: the counter
    // is 3-bit and wraps 7 -> 0 -> 1, so the old proxy re-reported "cold boot"
    // on the 8th and 9th boots of a session chain. g_coldBoot is the exact fact
    // (persist was NOT restored). The two flags are complementary on purpose:
    // every boot is exactly one of cold (persist lost) or soft (persist
    // survived a reset).
    if (g_coldBoot)  flags |= STATUS_COLD_BOOT;
    else             flags |= STATUS_SOFT_RESET;
    // The pending-uplink-failure latch, so PROD data frames carry the fault the
    // out-of-band diagnostic frame may not get to send for days. Before this,
    // STATUS_TX_TIMEOUT (and SOFT_RESET) had NO writer anywhere: the decoder
    // dutifully reported tx_timeout:false on every data frame through the whole
    // 2026-07-27/28 overnight failure storm. A defined wire flag nobody sets is
    // worse than none -- it actively asserts health.
    if (g_txFaultPending) flags |= STATUS_TX_TIMEOUT;
    solarPolicy.statusFlags_ = flags;
  }
  len += policy->appendPayload(payload + len);

  if (runMode == 1) {
    digitalWrite(LED_PIN, HIGH);
  }

  // Piggyback a network-time request on this uplink if one is pending. It only
  // arrives in an RX window after an uplink, and may not land -- onNetworkTime
  // leaves clockValid=0 in that case and we simply ask again next time.
  if (deviceTimeReqPending) {
    LMIC_requestNetworkTime(onNetworkTime, nullptr);
  }
  txComplete = false;
  logTxSchedState(F("data TX:"));
  lmic_tx_error_t txrc = LMIC_setTxData2(currentFPort, payload, len, 0);
  if (txrc != LMIC_ERROR_SUCCESS) {
    // Refused = nothing queued, no EV_TXCOMPLETE coming; see txFrameAndWait.
    // ramCount is preserved (uplinkScheduleOnTxSuccess not reached), so the
    // batch retries next cycle exactly as it does after a timeout.
    logPrint(F("FATAL: LMIC refused data uplink, rc="));
    logPrintln((int)txrc);
    g_txFaultPending = true;
    if (runMode == 1) digitalWrite(LED_PIN, LOW);
    return;
  }

  // Blocking wait for EV_TXCOMPLETE (with 2-minute safety timeout)
  uint32_t waitStart = millis();
  while (!txComplete && (millis() - waitStart < 120000UL)) {
    os_runloop_once();
  }

  // If we broke out due to timeout, clear the pending TX job to prevent a hung state
  if (!txComplete) {
    logPrintln(F("FATAL: TX Timeout. Clearing pending TX data."));
    logTxSchedState(F("data TIMEOUT:"));
    LMIC_clrTxData();
    g_txFaultPending = true;   // latched until a diagnostic frame reports it
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

// ---------------------------------------------------------------------------
// Diagnostics: a separate error/health uplink (diagnostics.h). The judgement --
// which faults, when to send, the rate limit -- is host-tested in the header;
// this glue only gathers live inputs and transmits.
// ---------------------------------------------------------------------------
static void gatherDiagInputs(DiagInputs *in) {
  in->isSolar        = (powerVariant == VARIANT_SOLAR);
  in->isDev          = (runMode == 1);
  in->resetCause     = g_resetCause;
  in->bootCounter    = persist.bootCounter;
  in->ds18Count      = g_ds18Count;
  // A real reading is finite and above the DS18B20 disconnect sentinel (-127).
  in->ds18Status       = g_ds18Status;
  in->tempImplausible  = g_tempImplausible;
  in->sensorFailStreak = persist.sensorFailStreak;
  in->ds18Rom[0] = g_ds18Rom[0];
  in->ds18Rom[1] = g_ds18Rom[1];
  in->ds18Rom[2] = g_ds18Rom[2];
  in->coldBoot       = g_coldBoot;
  in->persistCorrupt = g_persistCorrupt;
  in->ina219Present  = g_ina219Present;
  in->ina219ReadOk   = g_ina219ReadOk;
  in->ina219Ovf      = g_ina219Ovf;
  in->probeConfig    = g_probeConfig;
  in->clockValid     = persist.clockValid;
  in->lastTxTimeout  = g_txFaultPending;
  in->vbatMv         = (uint16_t)(VBAT_VOLTS() * 1000.0f);
}

// DEV-only: one line of LMIC TX-scheduling state, so a stalled TX is diagnosable
// from the serial log alone. Prints opmode plus how far in the future LMIC
// believes each throttle stamp lies (ms; negative = already passed). A healthy
// stack after an idle hour shows every delta well negative. Large POSITIVE
// deltas right after idling are the signature of the clock-extender starvation
// fixed in the DEV sleep loop (dev-note 20260728-1215).
static void logTxSchedState(const __FlashStringHelper *tag) {
  if (runMode != 1) return;
  ostime_t now = os_getTime();
  Serial.print(tag);
  Serial.print(F(" opmode=0x"));   Serial.print(LMIC.opmode, HEX);
  Serial.print(F(" txend_ms="));   Serial.print(osticks2ms(LMIC.txend - now));
  Serial.print(F(" gduty_ms="));   Serial.print(osticks2ms(LMIC.globalDutyAvail - now));
  for (uint8_t b = 0; b < MAX_BANDS; b++) {
    Serial.print(F(" band"));      Serial.print(b);
    Serial.print(F("_ms="));       Serial.print(osticks2ms(LMIC.bands[b].avail - now));
  }
  Serial.println();
}

// Send `len` bytes on `fport` and block for TXCOMPLETE (2-min timeout). Shared by
// the fault frame and the verbose frame. Out-of-band: never touches
// currentIntervalIndex or the uplink counter, so it cannot perturb the data
// cadence. Returns true only if the TX actually completed.
static bool txFrameAndWait(uint8_t fport, uint8_t *payload, uint8_t len) {
  if (!waitForTxReady(F("out-of-band frame:"))) {
    return false;
  }
  txComplete = false;
  logTxSchedState(F("oob TX:"));
  lmic_tx_error_t txrc = LMIC_setTxData2(fport, payload, len, 0);
  if (txrc != LMIC_ERROR_SUCCESS) {
    // Refused = nothing was queued (busy with a pending MAC answer, frame
    // infeasible, ...), so no EV_TXCOMPLETE will ever come. Waiting would burn
    // the full 2 minutes for nothing -- worse, a MAC-answer uplink completing
    // meanwhile would set txComplete and pass off as OUR frame. Bail now.
    logPrint(F("out-of-band frame: LMIC refused, rc="));
    logPrintln((int)txrc);
    g_txFaultPending = true;
    return false;
  }
  uint32_t waitStart = millis();
  while (!txComplete && (millis() - waitStart < 120000UL)) {
    os_runloop_once();
  }
  if (!txComplete) {
    logPrintln(F("out-of-band frame: TX timeout, clearing"));
    logTxSchedState(F("oob TIMEOUT:"));
    LMIC_clrTxData();
    g_txFaultPending = true;
    return false;
  }
  return true;
}

// Transmit one fault/diagnostic frame (FPort 1 PROD / 2 DEV).
static bool sendDiagFrame(const DiagInputs *in, uint16_t faults) {
  uint8_t payload[DIAG_PAYLOAD_LEN];
  diagEncode(payload, in, faults);
  return txFrameAndWait((runMode == 0) ? DIAG_FPORT_PROD : DIAG_FPORT_DEV,
                        payload, DIAG_PAYLOAD_LEN);
}

// Decide whether a diagnostic frame is due this cycle and, if so, send it.
// Call once per operational cycle, AFTER the data uplink is handled.
static void evaluateAndMaybeSendDiag() {
  DiagInputs in;
  gatherDiagInputs(&in);
  uint16_t faults = diagComputeFaults(&in);
  bool bootFrame = !bootDiagSent;
  uint32_t nowEpoch = persist.clockValid ? rtc.getEpoch() : 0;

  if (!diagShouldSend(bootFrame, faults,
                      persist.diagLastSentFaults, persist.diagLastSentEpoch,
                      nowEpoch, persist.clockValid, DIAG_MIN_RESEND_SECONDS)) {
    return;
  }

  logPrint(F("*** DIAGNOSTIC FRAME (fault bits="));
  logPrint(faults);
  logPrintln(F(") ***"));
  if (sendDiagFrame(&in, faults)) {
    diagMarkSent(&persist.diagLastSentFaults, &persist.diagLastSentEpoch,
                 faults, nowEpoch, persist.clockValid);
    persistSeal(&persist);   // the rate-limit latch must survive a reset
    bootDiagSent = true;     // the once-per-boot frame is now out
    // The frame that just went out carried the TX-fault latch (gathered into
    // in.lastTxTimeout above), so the fault is now REPORTED. Only here does the
    // latch clear -- a data-uplink success must not, or an overnight string of
    // failures vanishes from telemetry the moment one uplink gets through.
    g_txFaultPending = false;
  }
}

// Gather a full-state snapshot for the verbose DEV frame from live state.
static void gatherVerbose(VerboseSnapshot *v) {
  DiagInputs in; gatherDiagInputs(&in);
  bool solar = (powerVariant == VARIANT_SOLAR);
  v->isSolar       = solar;
  v->isDev         = (runMode == 1);
  v->coldBoot      = g_coldBoot;
  v->clockValid    = persist.clockValid;
  v->ina219Present = g_ina219Present;
  v->bonusActive   = solarPolicy.bonusActive_;
  v->busAmbiguous  = sensorBusAmbiguous;
  v->resetCause    = g_resetCause;
  v->bootCounter   = persist.bootCounter;
  v->intervalIndex = currentIntervalIndex;
  v->seasonState   = solar ? solarPolicy.seasonState_ : primaryPolicy.seasonState_;
  v->voltageBand   = solar ? solarPolicy.voltageState_ : primaryPolicy.voltageState_;
  v->batteryMv     = in.vbatMv;
  v->panelBusMv    = solar ? solarPolicy.lastBusMv_ : 0;
  float ma = solar ? solarPolicy.lastCurrentMa_ : 0.0f;
  if (ma < 0) ma = 0;
  uint32_t tenth = (uint32_t)(ma * 10.0f + 0.5f);
  v->panelCurrentTenthMa = tenth > 65535 ? 65535 : (uint16_t)tenth;
  float e = solarPolicy.ewma_;
  if (e < 0) e = 0; if (e > 1) e = 1;
  v->sunEwma255    = (uint8_t)(e * 255.0f + 0.5f);
  v->harvestMah    = solarPolicy.harvest_.totalMah;
  v->ina219Config  = g_probeConfig;
  v->ds18Count     = g_ds18Count;
  if (surfaceTempC == surfaceTempC && surfaceTempC > -100.0f) {
    float c = surfaceTempC * 100.0f;
    if (c > 32767.0f)  c = 32767.0f;
    if (c < -32768.0f) c = -32768.0f;
    v->surfaceTempCenti = (int16_t)c;
  } else {
    v->surfaceTempCenti = VERBOSE_TEMP_INVALID;
  }
  v->faults        = diagComputeFaults(&in);

  // ---- schema 2 (item 25) ----
  v->uptimeSeconds = millis() / 1000UL;   // DEV never sleeps: real elapsed time
  v->cycleCount    = g_cycleCount;
  v->ramCount      = uplinkSched.ramCount;
  v->uplinkCounter = uplinkScheduleCounterForPayload(&uplinkSched);
  v->panelStats    = &g_panelStats;       // n==0 (primary / just reset) -> bit clear
  v->gitHash24     = (uint32_t)FW_GIT_HASH24;
}

// One out-of-cycle panel sample for the schema-2 min/mean/max profile. Called
// from the DEV sleep loop roughly once a minute. Same wake -> CNVR -> read ->
// power-down sequence as the wake-time read; a failed sample is simply not
// added (the profile is a diagnostic, not a control input, so silence beats
// noise). The ~2 ms I2C transaction between os_runloop_once() calls is the
// same perturbation the wake-time read already makes, and the radio is idle.
static void samplePanelForStats() {
  if (powerVariant != VARIANT_SOLAR || runMode != 1) return;
#ifdef SOLAR_NO_INA219
  uint16_t adc = analogRead(PANEL_ADC_PIN);
  panelStatsAdd(&g_panelStats,
                (uint16_t)(adc * (3.3f / 1024.0f) * PANEL_DIV_RATIO * 1000.0f),
                0.0f);
#else
  uint16_t raw = 0;
  bool busReadOk = false;
  if (ina219WakeForReading(&raw, &busReadOk)) {
    float ma = ina219.getCurrent_mA();
    if (ina219.success()) {
      panelStatsAdd(&g_panelStats, ina219BusMillivolts(raw), ma);
    }
  }
  ina219.powerSave(true);
#endif
}

// Verbose full-state snapshot -- DEV-only, once at boot then ~hourly. Out-of-band.
// Called once per operational cycle after the fault-diagnostic evaluation, AND
// from inside the DEV sleep loop, so the hourly cadence holds even when the
// wake interval is longer than an hour (schema 2 / item 25). The millis() gate
// in verboseShouldSend() makes the extra call sites idempotent.
static void evaluateAndMaybeSendVerbose() {
  if (!verboseShouldSend(runMode == 1, verboseSentOnce, millis(),
                         lastVerboseMillis, VERBOSE_INTERVAL_MS)) {
    return;
  }
  // Due is not the same as allowed: after a failed attempt the frame stays due,
  // and this evaluator runs from every sleep-loop iteration, so attempts need
  // their own spacing or a wedged stack blocks the whole window back to back.
  if (!verboseRetryAllowed(verboseAttemptMade, millis(),
                           lastVerboseAttemptMillis, VERBOSE_RETRY_BACKOFF_MS)) {
    return;
  }
  verboseAttemptMade = true;
  lastVerboseAttemptMillis = millis();
  VerboseSnapshot v; gatherVerbose(&v);
  uint8_t payload[DIAG_VERBOSE_LEN];
  diagEncodeVerbose(payload, &v);
  logPrintln(F("*** VERBOSE DEV DIAGNOSTIC (FPort 3) ***"));
  if (txFrameAndWait(VERBOSE_FPORT_DEV, payload, DIAG_VERBOSE_LEN)) {
    lastVerboseMillis = millis();
    verboseSentOnce = true;
    // This frame carried the fault bitmap (gatherVerbose -> diagComputeFaults),
    // so the TX fault has now REACHED THE AIR -- clear the latch here too, not
    // only after a diagnostic frame.
    //
    // Without this the latch deadlocks against its own rate limiter:
    // diagShouldSend() refuses to send a diagnostic frame for a bit already in
    // persist.diagLastSentFaults, and the periodic re-alert is capped at
    // DIAG_MIN_RESEND_SECONDS -- so the only thing that could clear the latch is
    // suppressed precisely because the fault was once reported. Seen on
    // gisebo-05 2026-07-28: tx_timeout / healthy:false rode the 16:07, 17:07 and
    // 18:07 verbose frames with nothing having failed since 15:07, and would
    // have until the next day. It also made one old failure indistinguishable
    // from an hourly recurring one -- the opposite of the latch's purpose.
    //
    // Ordering is safe: gatherVerbose() snapshotted the faults BEFORE the TX, so
    // a frame that succeeded provably carried the bit, and a frame that failed
    // re-arms the latch inside txFrameAndWait().
    //
    // DEV-only in effect (the verbose frame does not exist in PROD), which is
    // the right asymmetry: DEV gets per-occurrence resolution for bench work,
    // PROD keeps the once-per-day limit that protects duty cycle and battery.
    if (v.faults & DIAG_FAULT_TX_TIMEOUT) g_txFaultPending = false;
    // The frame that just went out carried this hour's panel profile; start
    // accumulating the next one.
    panelStatsInit(&g_panelStats);
  }
}

void setup() {
  // Latch the reset cause first thing -- PM->RCAUSE holds why the MCU last reset
  // (power-on, brownout, external, watchdog, system). It persists until the next
  // reset, but read it before anything else touches the power manager. Reported
  // on the diagnostic frame so a field unit's reboots are attributable.
  g_resetCause = PM->RCAUSE.reg;

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
  // Capture WHY this is (or is not) a cold boot before persistInit overwrites it.
  // A true cold boot (wrong magic) is normal; decayed RAM (magic+version intact,
  // CRC bad) is a fault the diagnostic frame reports.
  g_persistCorrupt = persistDecayedButFramed(&persist);
  bool coldBoot = !persistValid(&persist);
  g_coldBoot = coldBoot;
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
  panelStatsInit(&g_panelStats);   // schema-2 profile accumulator
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

  // Derive this board's OTAA identity from its silicon serial, before LMIC uses
  // os_getDevEui()/os_getDevKey() during the join. In DEV, print the keys once
  // so the operator can register the device in TTN.
  deriveBoardCredentials();
  if (runMode == 1) {
    Serial.print(F("DevEUI (MSB, register in TTN): "));
    for (int i = 0; i < 8; i++) { if (g_creds.devEui[i] < 0x10) Serial.print('0'); Serial.print(g_creds.devEui[i], HEX); }
    Serial.println();
    Serial.print(F("AppKey (MSB): "));
    for (int i = 0; i < 16; i++) { if (g_creds.appKey[i] < 0x10) Serial.print('0'); Serial.print(g_creds.appKey[i], HEX); }
    Serial.println();
    Serial.println(F("JoinEUI: 0000000000000001"));
#ifdef FW_FIXED_KEYS
    Serial.println(F("*** FIXED-KEYS BUILD -- " FIXED_KEYS_LABEL " ONLY ***"));
    Serial.println(F("*** derived credentials are OVERRIDDEN; do not flash to any other board ***"));
#endif
    Serial.print(F("Firmware commit: "));
    if ((uint32_t)FW_GIT_HASH24 == 0) Serial.println(F("(unofficial build)"));
    else Serial.println((uint32_t)FW_GIT_HASH24, HEX);
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
  g_ds18Count = sensors.getDeviceCount();   // cached for the diagnostic frame
  sensorBusAmbiguous = (g_ds18Count > 1);
  ds18CaptureRom();
  if (sensorBusAmbiguous) {
    logPrint(F("ERROR: more than one DS18B20 on A2, found "));
    logPrintln(g_ds18Count);
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

  // Diagnostics: one frame per boot (after the first read, so the sensor/probe
  // inputs are populated), plus a rate-limited frame when a fault appears. Sent
  // after the data uplink so it never delays telemetry. Out-of-band: does not
  // touch the interval or the uplink counter.
  evaluateAndMaybeSendDiag();

  // Verbose full-state snapshot -- DEV-only, ~hourly. Also out-of-band.
  evaluateAndMaybeSendVerbose();

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
      // Feed LMIC's tick counter. os_runloop_once() samples the clock ONLY when
      // a job is scheduled; with an empty queue (all of this loop) it never
      // does. The HAL extends micros() -- which wraps every 71.6 min -- by
      // watching one bit that toggles every 35.8 min, so any unsampled gap
      // longer than that can swallow a whole micros() wrap and set os_getTime()
      // back 71.6 min. LMIC's duty/channel stamps then sit "in the future",
      // engineUpdate defers the next TX for up to that long, and the uplink
      // hits the 2-minute timeout with nothing on air. This is exactly what
      // gisebo-05 did all night 2026-07-27/28 (~2 of 3 hourly cycles lost, the
      // predicted 73% miss rate for a 62-min gap). PROD is immune: deep sleep
      // freezes micros(), so its gaps contain no elapsed time. One read per
      // iteration keeps the extender continuous. See
      // docs/dev-notes/20260728-1215_dev-sleep-starves-lmic-clock.md.
      (void)os_getTime();
      // Schema-2 panel profile: one sample a minute while we idle. And the
      // guaranteed-hourly verbose emission -- calling the evaluator here (it
      // is millis()-gated, so almost always a no-op) is what turns the old
      // max(1 h, wake interval) cadence into a real hourly one even at long
      // winter/degraded intervals. Both DEV-only by construction: this whole
      // branch is the DEV busy-wait, which PROD never enters.
      if (millis() - lastPanelSampleMillis >= PANEL_STATS_SAMPLE_MS) {
        lastPanelSampleMillis = millis();
        samplePanelForStats();
      }
      evaluateAndMaybeSendVerbose();
      delay(1); // Small delay to prevent watchdog resets
    }
  } else {
    // PROD MODE: True hardware deep sleep.
    LowPower.deepSleep(sleepIntervalSeconds * 1000UL);
  }

  logPrintln(F("Woke up!"));
}