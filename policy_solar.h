#pragma once
//
// SolarPolicy -- 1S2P 18650 li-ion + a vertical south-facing panel.
//
// 15-byte payload (6 appended after byte 8), FPorts 11/21. Selected at boot when
// the INA219 probe succeeds.
//
// The INA219 I2C read and the RTC dt come from the .ino (they need Arduino), and
// are fed in via ingestSample(). Everything with judgement -- the EWMA, the
// harvest accumulator, the interval decision, the payload -- is here and
// host-tested. onWake() is empty because the sensor read is the .ino's job.
//
#include "power_policy.h"
#include "solar_signal.h"

// Li-ion bands, replacing the primary 5.0/4.3/3.5. Healthy at 3.85 is ~60%+ SOC,
// where a solar-fed pack should live, so the bonus is usually available. Critical
// at 3.45 keeps margin above the Feather's ~3.4 V brownout. The 3.85 edge GATES
// the solar bonus, and li-ion sits on a flat 3.6-3.9 V plateau, so this edge sees
// the most dithering of any -- which is why voltageOffsetHyst matters most here.
#define LIION_HEALTHY_V  3.85f
#define LIION_LOW_V      3.65f
#define LIION_CRITICAL_V 3.45f

// The bonus subtracts a FIXED 2 steps, and the floor is index 2 (5 min).
//   Summer base 4 -> 2 (the floor)
//   Fall   base 5 -> 3
//   Winter base 7 -> 5
#define SOLAR_BONUS_STEPS 2
#define SOLAR_FLOOR_INDEX 2

// Status byte flag bits (low 5 bits; high 3 are the boot counter).
#define STATUS_COLD_BOOT   0x01
#define STATUS_SOFT_RESET  0x02
#define STATUS_CLOCK_VALID 0x04
#define STATUS_BONUS_ACTIVE 0x08
#define STATUS_TX_TIMEOUT  0x10

class SolarPolicy : public PowerPolicy {
public:
  void begin() override {
    seasonState_ = SEASON_SUMMER;
    voltageState_ = 0;
    // ewma_, harvest_, bonusActive_ are restored from .noinit by the .ino when
    // this is not a cold boot; begin() only sets the cold defaults.
    ewma_ = 0.0f;
    bonusActive_ = false;
    harvestInit(&harvest_);
  }

  // Sensor read is the .ino's job (I2C). Nothing to do in the interface hook.
  void onWake() override {}

  // Fed by the .ino inside the Dallas window, after the INA219 read.
  // Pure: updates the EWMA, the harvest accumulator, and the latched bonus.
  void ingestSample(uint16_t busMv, float currentMa, uint32_t dtSeconds) {
    lastBusMv_ = busMv;
    lastCurrentMa_ = currentMa;
    ewma_ = sunEwmaUpdate(ewma_, sunPresent(busMv), dtSeconds);
    bonusActive_ = sunBonusActive(ewma_, bonusActive_);
    harvestAdd(&harvest_, currentMa, dtSeconds);
  }

  uint8_t decideInterval(float tempC, float vbat) override {
    seasonState_ = seasonUpdate(seasonState_, tempC);

    static const float edges[3] = {LIION_HEALTHY_V, LIION_LOW_V, LIION_CRITICAL_V};
    voltageState_ = voltageOffsetHyst(vbat, voltageState_, edges);

    int idx = (int)seasonBaseIndex(seasonState_) + (int)voltageState_;

    // The bonus applies ONLY on a healthy pack (voltage_offset 0) AND with the
    // sun bonus latched on. Two gates so the signals cannot fight: sun never
    // shortens the interval on a struggling pack, and the loop self-corrects --
    // if shortening outruns harvest, the pack drains, voltage_offset leaves 0,
    // the bonus drops, and the interval returns to baseline.
    if (voltageState_ == 0 && bonusActive_) {
      idx -= SOLAR_BONUS_STEPS;
    }

    return clampIntervalIndex(idx, SOLAR_FLOOR_INDEX);
  }

  // Appends bytes 9..14. Returns 6.
  uint8_t appendPayload(uint8_t *buf) override {
    // Byte 9: panel bus voltage, 30 mV/LSB, 0..7.65 V.
    uint16_t mv = lastBusMv_;
    uint16_t vCode = mv / 30;
    buf[0] = vCode > 255 ? 255 : (uint8_t)vCode;

    // Byte 10: panel current, 0.5 mA/LSB, 0..127.5 mA.
    float ma = lastCurrentMa_ < 0 ? 0 : lastCurrentMa_;
    uint16_t iCode = (uint16_t)(ma / 0.5f + 0.5f);
    buf[1] = iCode > 255 ? 255 : (uint8_t)iCode;

    // Byte 11: sun EWMA, 0..255 = 0.0..1.0.
    float e = ewma_ < 0 ? 0 : (ewma_ > 1 ? 1 : ewma_);
    buf[2] = (uint8_t)(e * 255.0f + 0.5f);

    // Bytes 12-13: harvest accumulator, 1 mAh/LSB, big-endian to match the
    // temperature/battery fields' MSB-first convention.
    buf[3] = (uint8_t)(harvest_.totalMah >> 8);
    buf[4] = (uint8_t)(harvest_.totalMah & 0xFF);

    // Byte 14: status. High 3 bits boot counter, low 5 flags. The .ino sets
    // statusFlags_/bootCounter_ before TX; bonus-active is filled here from live
    // state so it cannot go stale.
    uint8_t status = (uint8_t)((bootCounter_ & 0x07) << 5);
    status |= (statusFlags_ & 0x1F);
    if (bonusActive_) status |= STATUS_BONUS_ACTIVE;
    buf[5] = status;

    return 6;
  }

  uint8_t fport(uint8_t runMode) override { return runMode == 0 ? 11 : 21; }

  // State. Public for .noinit persistence and host tests.
  uint8_t seasonState_ = SEASON_SUMMER;
  uint8_t voltageState_ = 0;
  float   ewma_ = 0.0f;
  bool    bonusActive_ = false;
  HarvestAccumulator harvest_ = {0.0f, 0};

  // Last sample, for the payload.
  uint16_t lastBusMv_ = 0;
  float    lastCurrentMa_ = 0.0f;

  // Set by the .ino before each TX.
  uint8_t bootCounter_ = 0;
  uint8_t statusFlags_ = 0;   // cold boot / soft reset / clock valid / tx timeout
};
