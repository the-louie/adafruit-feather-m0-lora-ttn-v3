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
#include <hal/hal.h>

#define VBATPIN A7
#define VBAT_VOLTS() (analogRead(VBATPIN) * (2.0f * 3.3f / 1024.0f))

#define STRAP_PIN 11
#define LED_PIN 13
#define ONE_WIRE_BUS A2

// Safely size the buffer to the protocol maximum to prevent future memmove
// overflows
#define MAX_BATCH 43

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);


// -----------------------------------------------------------------------------
// TTN OTAA CREDENTIALS (From your known-good config)
// -----------------------------------------------------------------------------
// AppEUI: Little-endian (LSB first)
static const u1_t PROGMEM APPEUI[8] = {0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
void os_getArtEui(u1_t *buf) { memcpy_P(buf, APPEUI, 8); }

// DevEUI: Little-endian (LSB first)
static const u1_t PROGMEM DEVEUI[8] = {0x01, 0x5E, 0x07, 0xD0, 0x7E, 0xD5, 0xB3, 0x70}; // 4
void os_getDevEui(u1_t *buf) { memcpy_P(buf, DEVEUI, 8); }

// AppKey: Big-endian (MSB first)
static const u1_t PROGMEM APPKEY[16] = {0x10, 0x14, 0x8B, 0x3A, 0x38, 0x5D, 0x46, 0xDA, 0xB3, 0x1E, 0xB6, 0x08, 0x1B, 0xD9, 0x26, 0x46};
void os_getDevKey(u1_t *buf) { memcpy_P(buf, APPKEY, 16); }

// Application state (set once in setup from STRAP_PIN)
static uint8_t runMode; // 0 = PROD, 1 = DEV
static uint32_t sleepIntervalSeconds;
static uint8_t batchTarget;
static uint8_t currentFPort;
static volatile bool txComplete = false;
static uint8_t wakeCounter = 0; // Sequence tracker
static uint32_t joinAttemptStart = 0;
static bool joinSuccessBlinkPending = false;

// Batch buffer: newest at index 0. Each entry 2 bytes (int16)
static uint8_t ramCount = 0;
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

// Helper to encode temperature
static uint16_t encodeTemperature(float tempC) {
  if (tempC < -50.0f || tempC != tempC)
    return 0xFFFFu;
  if (tempC < 0.0f)
    return 0xFFFEu;
  if (tempC > 30.0f)
    return 0xFFFDu;
  int v = (int)(tempC * 100.0f);
  if (v > 3000)
    v = 3000;
  if (v < 0)
    v = 0;
  return (uint16_t)v;
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

void readAndBufferSensors() {
  sensors.requestTemperatures();
  float tempC = sensors.getTempCByIndex(0);
  uint16_t encodedTemp = encodeTemperature(tempC);

  // Safely shift all existing data to the right by 1 position
  // Start from the end of the active buffer and move backwards
  int maxIndex = (ramCount >= batchTarget) ? (batchTarget - 1) : ramCount;
  for (int i = maxIndex; i > 0; i--) {
    dataBuffer[i] = dataBuffer[i - 1];
  }

  // Prepend newest reading at the front
  dataBuffer[0] = encodedTemp;

  if (ramCount < batchTarget) {
    ramCount++;
  }
  wakeCounter++;

  // Explicitly log the reading and buffer status
  logPrint(F("--> Measured Temp: "));
  logPrint(tempC);
  logPrint(F(" C. Buffer: "));
  logPrint(ramCount);
  logPrint(F("/"));
  logPrintln(batchTarget);
}

void transmitBatchAndWait() {
  if (LMIC.opmode & OP_TXRXPEND) {
    logPrintln(F("OP_TXRXPEND, skip send this cycle"));
    return;
  }

  uint16_t vbatCentivolts = (uint16_t)(VBAT_VOLTS() * 100.0f);
  const size_t payloadLen = 4 + (size_t)ramCount * 2;
  static uint8_t payload[4 + MAX_BATCH * 2];

  // Header (Big Endian format)
  payload[0] = (uint8_t)(vbatCentivolts >> 8);
  payload[1] = (uint8_t)(vbatCentivolts & 0xFF);
  payload[2] = runMode;     // Flags (0 = PROD, 1 = DEV)
  payload[3] = wakeCounter; // Sequence

  // Temperature Array (Big Endian format)
  for (uint8_t i = 0; i < ramCount; i++) {
    payload[4 + i * 2] = (uint8_t)(dataBuffer[i] >> 8);
    payload[4 + i * 2 + 1] = (uint8_t)(dataBuffer[i] & 0xFF);
  }

  // We slept physically, but LMIC time was frozen.
  // Force reset the duty cycle trackers to 0 so it doesn't illegally block us.
  LMIC.globalDutyAvail = 0;
  for (int i = 0; i < MAX_BANDS; i++) {
    LMIC.bands[i].avail = 0;
  }
  digitalWrite(LED_PIN, HIGH);
  txComplete = false;
  LMIC_setTxData2(currentFPort, payload, (uint8_t)payloadLen, 0);

  // Blocking wait for EV_TXCOMPLETE (with 2-minute safety timeout)
  uint32_t waitStart = millis();
  while (!txComplete && (millis() - waitStart < 120000UL)) {
    os_runloop_once();
  }

  // If we broke out due to timeout, force a MAC reset to clear the hung radio state
  if (!txComplete) {
    logPrintln(F("FATAL: TX Timeout. Forcing MAC reset."));
    LMIC_reset();
    LMIC_setClockError((uint32_t)MAX_CLOCK_ERROR * 5 / 100); // Restore after LMIC_reset
  }

  digitalWrite(LED_PIN, LOW);
  if (txComplete)
    ramCount = 0; // Clear buffer only after successful send; on timeout we retry next cycle
}

void setup() {
  // 1. Hardware stabilize
  delay(5000);
  SPI.begin();

  // 2. Determine Power & Mode
  // We use Pin 11. If it's GND, it's DEV; if floating, it's PROD.
  pinMode(STRAP_PIN, INPUT_PULLUP);
  delay(100); // Longer delay to ensure pull-up is stable

  // Intervals and FPort: same for both modes — 5 min measure, 15 min transmit (3 readings per batch)
  sleepIntervalSeconds = 300;
  batchTarget = 3;
  currentFPort = 10;

  // Read the strap: only runMode differs (USB/Serial, join timeout, sleep path still vary by mode)
  // LOW = Connected to GND (Development)
  // HIGH = Floating (Production)
  if (digitalRead(STRAP_PIN) == LOW) {
    runMode = 1; // DEV
  } else {
    runMode = 0; // PROD
  }

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
      NVIC_SystemReset(); // Start fresh
    }
    return;
  }

  // STATE 2: Operational
  if (joinSuccessBlinkPending) {
    joinSuccessBlinkPending = false;
    for (int i = 0; i < 5; i++) {
      digitalWrite(LED_PIN, HIGH);
      delay(50);
      digitalWrite(LED_PIN, LOW);
      delay(50);
    }
  }
  readAndBufferSensors();

  // "FAST-FLUSH" LOGIC:
  // If this is the very first reading after joining (wakeCounter == 1),
  // or if the buffer is full, send it immediately!
  if (wakeCounter == 1 || ramCount >= batchTarget) {
    logPrintln(F("*** TRIGGERING UPLINK (First Join or Buffer Full) ***"));
    transmitBatchAndWait();
  }

  // STATE 3: Sleep
  logPrint(F("Entering sleep for "));
  logPrint(sleepIntervalSeconds);
  logPrintln(F(" seconds..."));

  // Ensure serial finishes printing before sleep/delay
  if (runMode == 1) Serial.flush();

  if (runMode == 1) {
    // DEV MODE: Cannot use deepSleep while USB is active. Use a non-blocking wait.
    uint32_t waitStart = millis();
    while (millis() - waitStart < (sleepIntervalSeconds * 1000UL)) {
      delay(10); // Small delay keeps watchdog happy and lowers heat
    }
  } else {
    // PROD MODE: True hardware deep sleep.
    LowPower.deepSleep(sleepIntervalSeconds * 1000UL);
  }

  logPrintln(F("Woke up!"));
}