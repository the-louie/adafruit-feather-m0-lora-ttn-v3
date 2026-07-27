#!/usr/bin/env node
//
// Runs every decoder vector and exits non-zero if any fails.
//
//   node test/run.js
//
// There is no framework on purpose. A script with an exit code is enough, and
// it means this suite runs anywhere Node runs with no install step -- which
// matters, because the reason both production defects survived is that nothing
// executable ever checked the firmware<->decoder contract.

const path = require('path');
const { loadDecoder, diff } = require('./harness');

const REPO = path.join(__dirname, '..');
const DECODERS = {
  'gisebo-01': path.join(REPO, 'decoders', 'live-gisebo-01-9byte.js'),
  'gisebo-04': path.join(REPO, 'decoders', 'live-gisebo-04-8byte.js'),
  'gisebo-05': path.join(REPO, 'decoders', 'gisebo-05-v7.js'),
};

let passed = 0;
const failures = [];

function check(name, fn) {
  try {
    const problems = fn();
    if (problems && problems.length) {
      failures.push({ name, problems });
      console.log(`  FAIL  ${name}`);
      for (const p of problems) console.log(`          ${p}`);
    } else {
      passed++;
      console.log(`  ok    ${name}`);
    }
  } catch (err) {
    failures.push({ name, problems: [err.message] });
    console.log(`  ERROR ${name}`);
    console.log(`          ${err.message.split('\n').join('\n          ')}`);
  }
}

// ---------------------------------------------------------------------------
// 1. Live vectors: replay real production uplinks and assert our decoders
//    reproduce TTN's OWN decoded_payload.
//
//    Expected values come from production, not from us. If our understanding
//    of the protocol is wrong, these fail -- which is the entire point.
// ---------------------------------------------------------------------------
console.log('\nLive vectors (expected = TTN\'s own decoded_payload):');

const fixtures = require('./fixtures-live.json');
for (const v of fixtures.vectors) {
  const label = `${v.device} f_cnt=${v.fcnt} (${v.bytes.length}B, ` +
                `${v.expected.entries ? v.expected.entries.length : 0} entries)`;
  check(label, () => {
    const decodeUplink = loadDecoder(DECODERS[v.device]);
    const out = decodeUplink({ bytes: v.bytes, fPort: v.fPort, recvTime: v.recvTime });
    return diff(out.data, v.expected);
  });
}

// ---------------------------------------------------------------------------
// 1b. gisebo-05 v7 diagnostic frames (FPort 1 PROD / 2 DEV). Crafted vectors,
//     since no diagnostic firmware has flown yet; bytes follow diagnostics.h and
//     the field values are computed from the same spec the C encoder is tested
//     against. Replace with a live vector once a real diag frame lands.
// ---------------------------------------------------------------------------
console.log('\ngisebo-05 v7 diagnostic frames:');

const DIAG_VECTORS = [
  {
    name: 'healthy solar DEV boot frame (no faults)',
    fPort: 2,
    bytes: [1, 0x17, 0x01, 1, 1, 0x00, 0x00, 0x39, 0x9F, 0x10, 0x04],
    expected: {
      version: 7, frame: 'diagnostic', diag_schema: 1,
      mode: 'SOLAR', run_mode: 'DEV',
      cold_boot: true, clock_valid: false, ina219_seen: true,
      reset_cause: 1, reset_causes: ['power_on'],
      boot_counter: 1, ds18b20_count: 1,
      fault_bits: 0, faults: [], healthy: true,
      ina219_config: '0x399F', ina219_config_ok: true,
      battery_v: 4.1,
    },
  },
  {
    name: 'REAL gisebo-05 boot diagnostic (captured 2026-07-27T17:40:05Z, FPort 2)',
    fPort: 2,
    bytes: [0x01, 0x1F, 0x40, 0x01, 0x01, 0x00, 0x00, 0x39, 0x9F, 0x10, 0x71],
    expected: {
      version: 7, frame: 'diagnostic', diag_schema: 1,
      mode: 'SOLAR', run_mode: 'DEV',
      cold_boot: true, clock_valid: true, ina219_seen: true,
      reset_cause: 64, reset_causes: ['system'],
      boot_counter: 1, ds18b20_count: 1,
      fault_bits: 0, faults: [], healthy: true,
      ina219_config: '0x399F', ina219_config_ok: true,
      battery_v: 4.209,
    },
  },
  {
    name: 'primary PROD, DS18B20 missing + low battery, watchdog reset',
    fPort: 1,
    bytes: [1, 0x08, 0x20, 3, 0, 0x00, 0x41, 0x00, 0x00, 0x0C, 0xE4],
    expected: {
      version: 7, frame: 'diagnostic', diag_schema: 1,
      mode: 'PRIMARY', run_mode: 'PROD',
      cold_boot: false, clock_valid: true, ina219_seen: false,
      reset_cause: 32, reset_causes: ['watchdog'],
      boot_counter: 3, ds18b20_count: 0,
      fault_bits: 65, faults: ['ds18b20_not_found', 'low_battery'], healthy: false,
      ina219_config: '0x0000', ina219_config_ok: false,
      battery_v: 3.3,
    },
  },
];

for (const v of DIAG_VECTORS) {
  check(`diag: ${v.name}`, () => {
    const decodeUplink = loadDecoder(DECODERS['gisebo-05']);
    const out = decodeUplink({ bytes: v.bytes, fPort: v.fPort });
    const problems = diff(out.data, v.expected);
    if (out.errors && out.errors.length) problems.push(`unexpected errors: ${out.errors.join('; ')}`);
    return problems;
  });
}

check('diag: wrong length is rejected loudly', () => {
  const decodeUplink = loadDecoder(DECODERS['gisebo-05']);
  const out = decodeUplink({ bytes: [1, 0, 0, 0], fPort: 2 });
  const problems = [];
  if (!out.errors || out.errors.length === 0) problems.push('expected a length error');
  if (Object.keys(out.data).length !== 0) problems.push('expected empty data on error');
  return problems;
});

// Verbose DEV snapshot (FPort 3) -- full-state "all-clear" frame.
check('verbose: DEV full-state snapshot (FPort 3)', () => {
  const decodeUplink = loadDecoder(DECODERS['gisebo-05']);
  const out = decodeUplink({ bytes: [
    0x01, 0x3F, 0x40, 0x03, 0x04, 0x06, 0x10, 0x71, 0x13, 0xCE,
    0x00, 0x7D, 0x1F, 0x04, 0xD2, 0x39, 0x9F, 0x01, 0x08, 0x70, 0x00, 0x00],
    fPort: 3 });
  const problems = diff(out.data, {
    version: 7, frame: 'verbose', diag_schema: 1,
    mode: 'SOLAR', run_mode: 'DEV',
    cold_boot: true, clock_valid: true, ina219_seen: true, bonus_active: true, sensor_bus_ambiguous: false,
    reset_cause: 64, reset_causes: ['system'],
    boot_counter: 3, interval_index: 4, interval_minutes: 30,
    season: 'Summer', voltage_offset: 1,
    battery_v: 4.209, panel_v: 5.07, panel_ma: 12.5, sun_ewma: 0.122, harvest_mah: 1234,
    ina219_config: '0x399F', ina219_config_ok: true, ds18b20_count: 1,
    surface_temp: 21.6,
    fault_bits: 0, faults: [], healthy: true,
  });
  if (out.errors && out.errors.length) problems.push(`unexpected errors: ${out.errors.join('; ')}`);
  return problems;
});

check('verbose: wrong length rejected', () => {
  const decodeUplink = loadDecoder(DECODERS['gisebo-05']);
  const out = decodeUplink({ bytes: [1, 0, 0], fPort: 3 });
  return (out.errors && out.errors.length && Object.keys(out.data).length === 0) ? [] : ['expected a length error'];
});

// ---------------------------------------------------------------------------
// 2. Harness self-tests. A test harness that silently does nothing is worse
//    than no harness, so prove it fails when it should.
// ---------------------------------------------------------------------------
console.log('\nHarness self-tests:');

check('diff() catches a wrong value', () => {
  const problems = diff({ a: 1 }, { a: 2 });
  return problems.length === 1 ? [] : ['diff() should have reported exactly 1 problem'];
});

check('diff() catches a missing field', () => {
  const problems = diff({}, { a: 1 });
  return problems.length === 1 ? [] : ['diff() should have reported the missing field'];
});

check('diff() catches an unexpected extra field', () => {
  const problems = diff({ a: 1, b: 2 }, { a: 1 });
  return problems.length === 1 ? [] : ['diff() should have reported the extra field'];
});

check('diff() catches a short array', () => {
  const problems = diff({ e: [1, 2] }, { e: [1, 2, 3] });
  return problems.length > 0 ? [] : ['diff() should have reported the length mismatch'];
});

check('FIRMWARE_VERSION substitution fails loudly when absent', () => {
  // The live decoders have no such constant -- it arrives with gisebo-05 in
  // S02-06. Asking for one here MUST throw rather than silently test the file
  // as-is, or the suite would report green for a version it never exercised.
  try {
    loadDecoder(DECODERS['gisebo-01'], { firmwareVersion: 7 });
    return ['expected a throw, got none'];
  } catch (err) {
    return /no matching declaration/.test(err.message) ? [] : [`wrong error: ${err.message}`];
  }
});

check('FIRMWARE_VERSION substitution rewrites the constant', () => {
  const fs = require('fs');
  const os = require('os');
  const tmp = path.join(os.tmpdir(), 'fake-decoder.js');
  fs.writeFileSync(tmp,
    'const FIRMWARE_VERSION = 6;\n' +
    'function decodeUplink(input) { return { data: { v: FIRMWARE_VERSION }, warnings: [], errors: [] }; }\n');
  const asV7 = loadDecoder(tmp, { firmwareVersion: 7 });
  const asDefault = loadDecoder(tmp);
  fs.unlinkSync(tmp);
  const problems = [];
  if (asV7({}).data.v !== 7) problems.push(`substitution failed: got ${asV7({}).data.v}, want 7`);
  if (asDefault({}).data.v !== 6) problems.push(`default changed: got ${asDefault({}).data.v}, want 6`);
  return problems;
});

// ---------------------------------------------------------------------------
console.log(`\n${passed} passed, ${failures.length} failed\n`);
process.exit(failures.length === 0 ? 0 : 1);
