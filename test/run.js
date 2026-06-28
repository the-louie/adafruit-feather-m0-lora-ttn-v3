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
