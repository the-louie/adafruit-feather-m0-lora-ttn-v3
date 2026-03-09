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

// Two dummy reads let the SAMD21 ADC sampling capacitor settle through the 100k/100k divider.
static float getBatteryVoltage(void) {
  analogRead(VBATPIN);
  analogRead(VBATPIN);
  return analogRead(VBATPIN) * (2.0f * 3.3f / 1024.0f);
}

#define VBAT_VOLTS() getBatteryVoltage()

#define STRAP_PIN 11
#define LED_PIN 13
#define ONE_WIRE_BUS A2

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

static const u1_t PROGMEM DEVEUI[8] = {0xDD, 0x57, 0x07, 0xD0, 0x7E, 0xD5, 0xB3, 0x70}; // 1
//static const u1_t PROGMEM DEVEUI[8] = {0x01, 0x5E, 0x07, 0xD0, 0x7E, 0xD5, 0xB3, 0x70}; // 4
void os_getDevEui(u1_t *buf) { memcpy_P(buf, DEVEUI, 8); }

// AppKey: Big-endian (MSB first)
static const u1_t PROGMEM APPKEY[16] = {0x79, 0x87, 0x8E, 0x16, 0x19, 0xDA, 0xF4, 0x3C, 0xB6, 0x76, 0x71, 0xFC, 0x54, 0x68, 0xBE, 0xDB}; // 1
//static const u1_t PROGMEM APPKEY[16] = {0x10, 0x14, 0x8B, 0x3A, 0x38, 0x5D, 0x46, 0xDA, 0xB3, 0x1E, 0xB6, 0x08, 0x1B, 0xD9, 0x26, 0x46}; // 4




void os_getDevKey(u1_t *buf) { memcpy_P(buf, APPKEY, 16); }

// Interval index table: 0 = unused (5 min default if ever read), 1-10 = 1,5,15,30,60,120,360,720,1440,10080 minutes (in seconds)
static const uint32_t kIntervalSecondsByIndex[11] = {
    300, 60, 300, 900, 1800, 3600, 7200, 21600, 43200, 86400, 604800
};

// Temperature- and battery-controlled interval: thresholds and season base indices
#define TEMP_SUMMER_ENTER_C  16
#define TEMP_SUMMER_LEAVE_C 15
#define TEMP_MID_ENTER_C    8
#define TEMP_MID_LEAVE_C    7
#define VOLTAGE_HEALTHY_V   5.0f
#define VOLTAGE_LOW_V       4.3f
#define VOLTAGE_CRITICAL_V  3.5f
#define BASE_INDEX_SUMMER   4   // 30 min
#define BASE_INDEX_MID      5   // 60 min
#define BASE_INDEX_WINTER   7   // 360 min
#define MAX_INTERVAL_INDEX  10

// Application state (set once in setup from STRAP_PIN)
static uint8_t runMode; // 0 = PROD, 1 = DEV
static uint32_t sleepIntervalSeconds;
static uint8_t batchTarget;
static uint8_t currentIntervalIndex; // 0-10; from setup() and after successful uplink (temperature/battery algorithm)
static uint8_t currentFPort;
static uint8_t current_season_state; // 0=Winter, 1=Fall/Spring, 2=Summer; updated only in interval calculation
static float lastTempC;              // Latest water temp from readAndBufferSensors(); used when applying next interval
static volatile bool txComplete = false;
static uint8_t wakeCounter; // 4-bit sequence 0..15 for protocol; set to 0 in setup()
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

// Returns raw centidegrees -1000..3000 (-10°C..+30°C), or sentinels: 0xFFFF invalid/NaN,
// values < -10°C → too cold (251), > +30°C → too warm (252). Used for 9-byte protocol.
static int16_t encodeTemperature(float tempC) {
  if (tempC != tempC || tempC < -50.0f)
    return (int16_t)0xFFFF;
  if (tempC < -10.0f)
    return (int16_t)0xFFFE; // too cold → 251
  if (tempC > 30.0f)
    return (int16_t)0xFFFD; // too warm → 252
  int v = (int)(tempC * 100.0f);
  if (v > 3000) v = 3000;
  if (v < -1000) v = -1000;
  return (int16_t)v;
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

  // PROD: USB detached; low-power idle during Dallas conversion. DEV: keep USB and LMIC alive with os_runloop_once().
  if (runMode == 0) {
    LowPower.idle(750);
  } else {
    uint32_t startWait = millis();
    while (millis() - startWait < 750) {
      os_runloop_once();
      delay(1); // Small delay to prevent watchdog resets
    }
  }

  float tempC = sensors.getTempCByIndex(0);
  lastTempC = tempC;
  int16_t encodedTemp = encodeTemperature(tempC);

  // Shift right by 1; only indices 1..5 (never 6) to avoid buffer overrun
  for (int i = 5; i > 0; i--) {
    dataBuffer[i] = dataBuffer[i - 1];
  }
  dataBuffer[0] = (uint16_t)encodedTemp;

  if (ramCount < batchTarget) {
    ramCount++;
  }
  wakeCounter = (wakeCounter + 1) & 0x0F; // 4-bit sequence for protocol compatibility

  // Explicitly log the reading and buffer status
  logPrint(F("--> Measured Temp: "));
  logPrint(tempC);
  logPrint(F(" C. Buffer: "));
  logPrint(ramCount);
  logPrint(F("/"));
  logPrintln(batchTarget);
}

// Computes next interval index from water temp and battery voltage. Season state uses 1 C hysteresis
// to avoid flapping at boundaries. Cold-weather battery illusion: low voltage in cold does not
// permanently lock long interval; when temp rises, season can transition and base can shorten.
// Final index is clamped to 1..MAX_INTERVAL_INDEX (memory-crash edge case: winter + critical battery).
static uint8_t calculate_interval_index(float tempC, float voltageV) {
  // Invalid/NaN temp: do not change season state; use previous state for base index
  if (tempC != tempC || tempC < -50.0f || tempC > 60.0f) {
    // Keep current_season_state unchanged; fall through to base index from it
  } else {
    if (current_season_state == 2 && tempC < (float)TEMP_SUMMER_LEAVE_C) {
      current_season_state = 1;
    } else if (current_season_state == 1 && tempC >= (float)TEMP_SUMMER_ENTER_C) {
      current_season_state = 2;
    } else if (current_season_state == 1 && tempC < (float)TEMP_MID_LEAVE_C) {
      current_season_state = 0;
    } else if (current_season_state == 0 && tempC >= (float)TEMP_MID_ENTER_C) {
      current_season_state = 1;
    }
  }

  uint8_t base_index;
  if (current_season_state == 2) {
    base_index = BASE_INDEX_SUMMER;
  } else if (current_season_state == 1) {
    base_index = BASE_INDEX_MID;
  } else {
    base_index = BASE_INDEX_WINTER;
  }

  uint8_t voltage_offset = 0;
  if (voltageV >= VOLTAGE_HEALTHY_V) {
    voltage_offset = 0;
  } else if (voltageV >= VOLTAGE_LOW_V) {
    voltage_offset = 1;
  } else if (voltageV >= VOLTAGE_CRITICAL_V) {
    voltage_offset = 2;
  } else {
    voltage_offset = 3;
  }

  uint8_t final_index = base_index + voltage_offset;
  if (final_index > MAX_INTERVAL_INDEX) {
    final_index = MAX_INTERVAL_INDEX;
  }
  if (final_index < 1) {
    final_index = 1;  // Index 0 is unused; always schedule with at least 1 min
  }
  return final_index;
}

void transmitBatchAndWait() {
  if (LMIC.opmode & OP_TXRXPEND) {
    logPrintln(F("OP_TXRXPEND, skip send this cycle"));
    return;
  }

  // Protocol: 9 bytes. Byte 0 = interval index (0-10). Bytes 1-2 = 12-bit battery offset + 4-bit sequence. Bytes 3-8 = 6 temps: 250 null, 251 too cold, 252 too warm, else (centidegrees/100+10)/0.2.
  static uint8_t payload[9];
  uint8_t intervalByte = (currentIntervalIndex > 10) ? 10 : currentIntervalIndex;
  payload[0] = intervalByte;

  uint32_t vbat_mv = (uint32_t)(VBAT_VOLTS() * 1000.0f);
  int32_t offset = (int32_t)vbat_mv - 3000;
  if (offset < 0) offset = 0;
  if (offset > 4095) offset = 4095;
  uint16_t uoffset = (uint16_t)offset;
  payload[1] = (uint8_t)(uoffset >> 4);
  payload[2] = (uint8_t)(((uoffset & 0x0FU) << 4) | (wakeCounter & 0x0FU));

  for (uint8_t i = 0; i < 6; i++) {
    if (i >= ramCount) {
      payload[3 + i] = 250;
      continue;
    }
    uint16_t raw = dataBuffer[i];
    if (raw == 0xFFFFu) {
      payload[3 + i] = 250;
    } else if (raw == 0xFFFEu) {
      payload[3 + i] = 251;
    } else if (raw == 0xFFFDu) {
      payload[3 + i] = 252;
    } else {
      float degC = (int16_t)raw / 100.0f;
      if (degC < -10.0f) payload[3 + i] = 251;
      else if (degC > 30.0f) payload[3 + i] = 252;
      else payload[3 + i] = (uint8_t)((degC + 10.0f) / 0.2f + 0.5f);
    }
  }

  if (runMode == 1) {
    digitalWrite(LED_PIN, HIGH);
  }
  txComplete = false;
  LMIC_setTxData2(currentFPort, payload, 9, 0);

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
    float vbat_volts = (float)vbat_mv / 1000.0f;
    uint8_t nextIndex = calculate_interval_index(lastTempC, vbat_volts);
    currentIntervalIndex = nextIndex;
    sleepIntervalSeconds = kIntervalSecondsByIndex[currentIntervalIndex];
    ramCount = 0; // Clear buffer only after successful send; on timeout we retry next cycle
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

  currentIntervalIndex = 2; // 5 min initial; then from temperature/battery algorithm after each successful TX
  current_season_state = 2; // Summer; first TX will use real temp and hysteresis
  lastTempC = 15.0f;       // Safe default until first reading
  uint8_t intervalIdx = (currentIntervalIndex > 10) ? 10 : currentIntervalIndex;
  sleepIntervalSeconds = kIntervalSecondsByIndex[intervalIdx];
  batchTarget = 6;
  wakeCounter = 0;
  ramCount = 0;

  // Read the strap: only runMode differs (USB/Serial, join timeout, sleep path still vary by mode)
  // LOW = Connected to GND (Development)
  // HIGH = Floating (Production)
  if (digitalRead(STRAP_PIN) == LOW) {
    runMode = 1; // DEV
  } else {
    runMode = 0; // PROD
  }
  currentFPort = (runMode == 0) ? 10 : 20; // 10 = PROD, 20 = DEV

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