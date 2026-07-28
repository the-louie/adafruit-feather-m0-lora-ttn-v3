#!/usr/bin/env node
// v7 decoder tests for gisebo-05. There is no production v7 data yet, so vectors
// are built here from the same field encodings the firmware uses (payload.h,
// policy_solar.h) and the decoder is asserted field-by-field.
//
//   node test/run_v7.js

const path = require('path');
const { loadDecoder } = require('./harness');

const DECODER = path.join(__dirname, '..', 'decoders', 'gisebo-05-v7.js');
const decode = loadDecoder(DECODER);

let passed = 0;
const failures = [];
function check(name, cond) {
  if (cond) { passed++; console.log(`  ok    ${name}`); }
  else { failures.push(name); console.log(`  FAIL  ${name}`); }
}
function near(a, b, eps = 1e-6) { return Math.abs(a - b) < eps; }

const RECV = "2026-07-17T12:00:00.000Z";   // fixed, for stable timestamps/clarity

console.log("\nv7 decoder -- primary (9-byte)");
{
  // interval 4, battery 5.768 (offset 2768 -> b1=173,b2 hi nibble 0), counter 5,
  // six temps 16.8 C (134).
  const b2 = ((2768 & 0x0f) << 4) | 5;         // low nibble battery + counter 5
  const bytes = [4, 2768 >> 4, b2, 134, 134, 134, 134, 134, 134];
  const r = decode({ bytes, fPort: 10, recvTime: RECV });
  check("primary: no errors", r.errors.length === 0);
  check("primary: version 7", r.data.version === 7);
  check("primary: mode PRIMARY", r.data.mode === "PRIMARY");
  check("primary: interval_index 4", r.data.interval_index === 4);
  check("primary: interval_minutes 30", r.data.interval_minutes === 30);
  check("primary: battery 5.768", near(r.data.battery_v, 5.768));
  check("primary: uplink_counter 5 (not 'sequence')", r.data.uplink_counter === 5 && r.data.sequence === undefined);
  check("primary: 6 entries", r.data.entries.length === 6);
  check("primary: entry temp 16.8", near(r.data.entries[0].temperature, 16.8));
  check("primary: no solar fields", r.data.panel_v === undefined && r.data.clarity === undefined);
}

console.log("\nv7 decoder -- solar (15-byte)");
{
  // interval 2, battery 3.900 (offset 900), counter 3, six temps 18.0 (140),
  // panel 4800 mV (160), 25 mA (50), ewma 0.6 (153), harvest 300 (0x012C),
  // status: boot 2, clock valid + bonus active = (2<<5)|0x04|0x08 = 76.
  const b2 = ((900 & 0x0f) << 4) | 3;
  const bytes = [2, 900 >> 4, b2, 140, 140, 140, 140, 140, 140,
                 160, 50, 153, 0x01, 0x2C, 76];
  const r = decode({ bytes, fPort: 11, recvTime: RECV });
  check("solar: no errors", r.errors.length === 0);
  check("solar: version 7, mode SOLAR", r.data.version === 7 && r.data.mode === "SOLAR");
  check("solar: interval 2 / 5 min", r.data.interval_index === 2 && r.data.interval_minutes === 5);
  check("solar: battery 3.900", near(r.data.battery_v, 3.900));
  check("solar: uplink_counter 3", r.data.uplink_counter === 3);
  check("solar: 6 entries at 18.0", r.data.entries.length === 6 && near(r.data.entries[0].temperature, 18.0));
  check("solar: panel_v 4.800", near(r.data.panel_v, 4.8));
  check("solar: panel_ma 25.0", near(r.data.panel_ma, 25.0));
  check("solar: sun_ewma 0.6", near(r.data.sun_ewma, 0.6, 0.01));
  check("solar: harvest 300", r.data.harvest_mah === 300);
  check("solar: boot_counter 2", r.data.boot_counter === 2);
  check("solar: clock_valid true", r.data.clock_valid === true);
  check("solar: bonus_active true", r.data.bonus_active === true);
  check("solar: cold_boot false", r.data.cold_boot === false);
  check("solar: clarity computed (clock valid)", typeof r.data.clarity === "number");
  check("solar: expected_daylight_fraction present", typeof r.data.expected_daylight_fraction === "number");
}

console.log("\nv7 decoder -- the charge-terminated case (must not read as null/dark)");
{
  // Full pack in bright sun: 0 mA, panel Voc 6000 mV -> code 200. EWMA high.
  const b2 = ((900 & 0x0f) << 4) | 0;
  const bytes = [2, 900 >> 4, b2, 140, 140, 140, 140, 140, 140,
                 200, 0, 230, 0, 0, 0x04 /*clock valid only*/];
  const r = decode({ bytes, fPort: 11, recvTime: RECV });
  check("charge-terminated: panel_v ~6.0 despite 0 current", near(r.data.panel_v, 6.0));
  check("charge-terminated: panel_ma 0", r.data.panel_ma === 0);
  check("charge-terminated: sun_ewma high (~0.9)", r.data.sun_ewma > 0.85);
  check("charge-terminated: clarity reflects real sun, not 0", r.data.clarity > 0);
}

console.log("\nv7 decoder -- clarity does NOT depend on the device clock");
{
  // This assertion was deliberately INVERTED on 2026-07-28. It used to require
  // clarity === null when the device clock was invalid. That was wrong: clarity
  // is sun_ewma / expectedDaylightFraction(recvTime, latitude), and neither
  // operand touches the device RTC -- recvTime is stamped by the network server
  // and sun_ewma decays against sleepIntervalSeconds. The old gate blanked the
  // panel-obscured signal for a unit's entire early life. See TODO 24 / dev-note
  // 20260728-1900_clarity-off-the-device-clock.md.
  const b2 = ((900 & 0x0f) << 4) | 0;
  const bytes = [2, 900 >> 4, b2, 140, 140, 140, 140, 140, 140,
                 160, 50, 100, 0, 0, 0x00 /*no clock valid*/];
  const r = decode({ bytes, fPort: 11, recvTime: RECV });
  check("clock invalid: clarity is STILL computed", typeof r.data.clarity === "number");
  check("clock invalid: clock_valid itself still decodes false", r.data.clock_valid === false);
  check("clock invalid: expected_daylight_fraction present", typeof r.data.expected_daylight_fraction === "number");
}

console.log("\nv7 decoder -- no recvTime -> clarity null (the only real dependency)");
{
  const b2 = ((900 & 0x0f) << 4) | 0;
  const bytes = [2, 900 >> 4, b2, 140, 140, 140, 140, 140, 140,
                 160, 50, 100, 0, 0, 0x04 /*clock valid*/];
  const r = decode({ bytes, fPort: 11 });   // no recvTime
  check("no recvTime: clarity is null", r.data.clarity === null);
}

console.log("\nv7 decoder -- length/FPort mismatch errors");
{
  check("15 bytes on FPort 10 errors", decode({ bytes: new Array(15).fill(0), fPort: 10 }).errors.length > 0);
  check("9 bytes on FPort 11 errors", decode({ bytes: new Array(9).fill(0), fPort: 11 }).errors.length > 0);
  check("unknown FPort errors", decode({ bytes: new Array(9).fill(0), fPort: 99 }).errors.length > 0);
}

console.log("\nv7 decoder -- out-of-contract temperature byte (S05-20)");
{
  const b2 = ((900 & 0x0f) << 4) | 0;
  // byte 3 = 220, which is 201-249: out of contract, must be reported not dropped.
  const bytes = [2, 900 >> 4, b2, 220, 140, 140, 140, 140, 140];
  const r = decode({ bytes, fPort: 10, recvTime: RECV });
  const oor = r.data.entries.find(e => e.temperature_state === "out of range");
  check("out-of-range byte reported explicitly, not dropped", oor && oor.raw === 220);
  check("...and later slots still present (position preserved)", r.data.entries.length === 6);
}

console.log(`\n${passed} passed, ${failures.length} failed\n`);
process.exit(failures.length ? 1 : 0);
