// DS18B20 bench diagnostic -- Feather M0 + DS18B20 (+ li-ion for the battery readout).
//
// No LoRa, no TTN, no gateway, no INA219. This exists to do two things on the
// bench in five minutes:
//   1. Confirm the DS18B20 is wired correctly.
//   2. SHOW the idle(750) defect and its delay(750) fix, live over USB serial.
//      This is S06-02 / S06-13 done cheaply -- the check 139 production uplinks
//      could never give us.
//
// Wire the DS18B20 (3-wire, normal power):
//   DATA (yellow) -> A2      VDD (red) -> 3V      GND (black) -> GND
//   4.7k resistor between DATA and VDD   <-- MANDATORY, the bus fails without it
//
// Open the Serial Monitor at 115200 baud.

#include <OneWire.h>
#include <DallasTemperature.h>
#include <ArduinoLowPower.h>
#include <math.h>

#define ONE_WIRE_BUS A2
#define VBATPIN      A7

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

static float readBatteryVolts() {
  analogRead(VBATPIN);
  analogRead(VBATPIN);
  return analogRead(VBATPIN) * (2.0f * 3.3f / 1024.0f);
}

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 5000) { }
  Serial.println();
  Serial.println(F("=== DS18B20 bench diagnostic ==="));

  sensors.begin();
  int n = sensors.getDeviceCount();
  Serial.print(F("Sensors found on A2: "));
  Serial.println(n);

  if (n == 0) {
    Serial.println(F("NONE found. Check DATA->A2, VDD->3V, GND->GND, and the"));
    Serial.println(F("4.7k pull-up between DATA and VDD. Halting."));
    while (true) { }
  }
  if (n > 1) {
    Serial.println(F("More than one on the bus -- the real firmware would report"));
    Serial.println(F("nulls here, since it cannot tell which is the surface sensor."));
  }

  Serial.print(F("Battery on A7: "));
  Serial.print(readBatteryVolts(), 3);
  Serial.println(F(" V   (li-ion should read ~3.0-4.2 V)"));
  Serial.println();

  // --- One-shot: the 85 C power-on signature -------------------------------
  // The DS18B20 scratchpad powers up holding +85.00 C. Before any conversion
  // has finished, a read returns exactly that. If idle(750) returns early (the
  // bug), the very first read happens BEFORE the conversion completes -> 85 C.
  Serial.println(F("First read after boot, the two ways:"));

  sensors.requestTemperatures();
  LowPower.idle(750);                       // truncates to a 0-second alarm -> early return
  float firstIdle = sensors.getTempCByIndex(0);

  delay(1500);                              // let it fully settle
  sensors.requestTemperatures();
  delay(750);                               // actually waits
  float firstDelay = sensors.getTempCByIndex(0);

  Serial.print(F("  idle(750)  = "));  Serial.print(firstIdle, 2);  Serial.println(F(" C"));
  Serial.print(F("  delay(750) = "));  Serial.print(firstDelay, 2); Serial.println(F(" C"));
  if (firstIdle >= 84.0f && firstIdle <= 86.0f) {
    Serial.println(F("  >> idle path reads ~85 C = the power-on default. It read"));
    Serial.println(F("     BEFORE the conversion finished. THAT is the bug, live."));
  }
  Serial.println();
  Serial.println(F("Now watch the ongoing lag. Pinch the sensor to warm it and"));
  Serial.println(F("watch idle(750) trail delay(750) by one cycle:"));
  Serial.println();
}

void loop() {
  // idle(750) path: returns early, so this read reflects the PREVIOUS conversion.
  sensors.requestTemperatures();
  LowPower.idle(750);
  float lagged = sensors.getTempCByIndex(0);

  // delay(750) path: actually waits, so this read is fresh.
  sensors.requestTemperatures();
  delay(750);
  float fresh = sensors.getTempCByIndex(0);

  Serial.print(F("idle(750): "));
  Serial.print(lagged, 2);
  Serial.print(F(" C    delay(750): "));
  Serial.print(fresh, 2);
  Serial.print(F(" C"));
  if (fabs(lagged - fresh) > 0.4f) {
    Serial.print(F("   <-- lagging by "));
    Serial.print(fabs(lagged - fresh), 2);
    Serial.print(F(" C"));
  }
  Serial.println();

  delay(2000);
}
