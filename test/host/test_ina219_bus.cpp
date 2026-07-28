// Host tests for ina219_bus.h -- the bus-voltage register (02h) interpretation.
//
// The register values here are constructed from the datasheet layout (SBOS448G
// Figure 24: voltage in bits 15..3 at 4 mV/LSB, CNVR bit 1, OVF bit 0), plus
// one taken from the wire: gisebo-05's frozen 3852 mV night.

#include <cstdio>
#include "ina219_bus.h"

static int failures = 0;
static void check(bool ok, const char *name) {
  std::printf("  %-4s  %s\n", ok ? "ok" : "FAIL", name);
  if (!ok) failures = 1;
}

// Compose a raw register value from a voltage in mV plus flag bits.
static uint16_t rawFrom(uint16_t mv, bool cnvr, bool ovf) {
  uint16_t counts = (uint16_t)(mv / 4u);
  uint16_t raw = (uint16_t)(counts << 3);
  if (cnvr) raw |= INA219_BUS_CNVR_MASK;
  if (ovf)  raw |= INA219_BUS_OVF_MASK;
  return raw;
}

int main() {
  std::printf("\nina219_bus -- register 02h interpretation\n");

  // ---- voltage math ----
  check(ina219BusMillivolts(rawFrom(3852, false, false)) == 3852,
        "3852 mV round-trips (the frozen night value, now read honestly)");
  check(ina219BusMillivolts(rawFrom(0, true, false)) == 0,
        "0 mV round-trips (dark panel)");
  // Datasheet full-scale example: 16 V FSR = decimal 4000 counts = 0FA0h.
  check(ina219BusMillivolts((uint16_t)(0x0FA0 << 3)) == 16000,
        "datasheet 16 V FSR example: 0x0FA0 counts -> 16000 mV");
  check(ina219BusMillivolts(rawFrom(5100, true, false)) == 5100,
        "5100 mV (panel Voc at charge termination) round-trips");
  // The flag bits must not bleed into the voltage.
  check(ina219BusMillivolts(rawFrom(3852, true, true)) == 3852,
        "CNVR/OVF bits do not perturb the voltage field");

  // ---- flag extraction ----
  check(ina219BusConversionReady(rawFrom(3852, true, false)),  "CNVR set is seen");
  check(!ina219BusConversionReady(rawFrom(3852, false, false)), "CNVR clear is seen");
  check(ina219BusOverflow(rawFrom(3852, false, true)),  "OVF set is seen");
  check(!ina219BusOverflow(rawFrom(3852, false, false)), "OVF clear is seen");

  std::printf("\nina219_bus -- the live-read verdict\n");

  // The healthy daytime case.
  check(ina219LiveReadOk(true, true, true, 3990),
        "healthy daytime read passes");
  // THE defect this replaces: a powered-down part serves stale registers and
  // never sets CNVR after the wake. Everything else looks fine.
  check(!ina219LiveReadOk(true, false, true, 3852),
        "no conversion since wake -> REJECTED (the frozen-register case)");
  // A dark panel is not a fault -- that is the sun signal working.
  check(ina219LiveReadOk(true, true, true, 0),
        "dark panel at 0 mV passes (no lower bound, by design)");
  // I2C-level failures.
  check(!ina219LiveReadOk(false, false, false, 0), "bus read NAK -> rejected");
  check(!ina219LiveReadOk(true, true, false, 3990), "current read NAK -> rejected");
  // A wedged-but-ACKing bus serving garbage.
  check(!ina219LiveReadOk(true, true, true, 24000), ">= 20 V garbage -> rejected");

  // OVF is deliberately NOT in the verdict: bus voltage stays valid under
  // overflow, so the EWMA keeps its input. The caller handles current + fault.
  check(ina219LiveReadOk(true, true, true, 3990),
        "verdict is independent of OVF (bus voltage valid under overflow)");

  // Timeout constant sanity: must exceed the configured worst-case conversion
  // (586 us x 2 channels) with real margin, and stay well under the Dallas
  // window it borrows from.
  check(INA219_CNVR_TIMEOUT_MS >= 5 && INA219_CNVR_TIMEOUT_MS <= 100,
        "CNVR timeout is sane for 12-bit single-sample mode");

  std::printf("\n%s\n\n", failures ? "FAILED" : "all passed");
  return failures;
}
