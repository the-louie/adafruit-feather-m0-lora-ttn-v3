// Loads a TTN payload formatter and makes its decodeUplink() callable from Node.
//
// Why it loads the file as TEXT and evals it, rather than require()-ing it:
// a TTN formatter is a bare script with a top-level `function decodeUplink(...)`.
// It has no module.exports, because the TTN console does not want one. If we
// added exports so Node could require() it, the file we test would no longer be
// the file we paste into TTN -- and a decoder that drifted from what actually
// ran is exactly how we ended up with ttn-decoder-v6.js. So: test the real file.

const fs = require('fs');
const vm = require('vm');

// The per-device firmware version constant (introduced for gisebo-05 in S02-06).
// TTN formatters take no configuration, so a top-level const is the only knob
// the console supports. To test v5/v6/v7 semantics against ONE file we rewrite
// that line before eval. This is deliberately blunt; see loadDecoder() below.
const VERSION_LINE = /^(\s*const\s+FIRMWARE_VERSION\s*=\s*)(\d+)(\s*;.*)$/m;

/**
 * Load a decoder and return its decodeUplink function.
 *
 * @param {string} filePath           path to the formatter .js
 * @param {object} [opts]
 * @param {number} [opts.firmwareVersion]  if set, rewrite FIRMWARE_VERSION to this
 * @returns {function} decodeUplink(input) -> {data, warnings, errors}
 */
function loadDecoder(filePath, opts = {}) {
  let source = fs.readFileSync(filePath, 'utf8');

  if (opts.firmwareVersion !== undefined) {
    if (!VERSION_LINE.test(source)) {
      // Fail loudly. If the declaration is reformatted or renamed, silently
      // testing the file's built-in default would mean the suite reports green
      // for a version it never actually exercised. That is worse than no test.
      throw new Error(
        `harness: asked for FIRMWARE_VERSION=${opts.firmwareVersion} but no ` +
        `matching declaration in ${filePath}.\n` +
        `Expected a line like:  const FIRMWARE_VERSION = 7;\n` +
        `Keep it on its own line, in that exact form.`
      );
    }
    source = source.replace(VERSION_LINE, `$1${opts.firmwareVersion}$3`);
  }

  // Run in a fresh sandbox so one decoder cannot leak globals into another.
  const sandbox = {};
  vm.createContext(sandbox);
  vm.runInContext(source, sandbox, { filename: filePath });

  if (typeof sandbox.decodeUplink !== 'function') {
    throw new Error(`harness: ${filePath} defines no decodeUplink() function`);
  }
  return sandbox.decodeUplink;
}

/**
 * Deep-compare actual vs expected. Returns a list of human-readable
 * differences; empty list means they match.
 *
 * Written out longhand rather than pulling in a matcher library: when a vector
 * fails at 2am, "entries[3].temperature: expected 17, got 16.8" is the whole
 * point, and it should not depend on how someone configured a framework.
 */
function diff(actual, expected, path = '') {
  const problems = [];

  if (expected === null || typeof expected !== 'object') {
    if (actual !== expected) {
      problems.push(`${path || '(root)'}: expected ${JSON.stringify(expected)}, got ${JSON.stringify(actual)}`);
    }
    return problems;
  }

  if (Array.isArray(expected)) {
    if (!Array.isArray(actual)) {
      problems.push(`${path}: expected an array of ${expected.length}, got ${JSON.stringify(actual)}`);
      return problems;
    }
    if (actual.length !== expected.length) {
      problems.push(`${path}: expected ${expected.length} items, got ${actual.length}`);
    }
    const n = Math.min(actual.length, expected.length);
    for (let i = 0; i < n; i++) {
      problems.push(...diff(actual[i], expected[i], `${path}[${i}]`));
    }
    return problems;
  }

  if (actual === null || typeof actual !== 'object') {
    problems.push(`${path}: expected an object, got ${JSON.stringify(actual)}`);
    return problems;
  }

  for (const key of Object.keys(expected)) {
    problems.push(...diff(actual[key], expected[key], path ? `${path}.${key}` : key));
  }
  // Extra keys are reported too. A decoder that starts emitting a field nobody
  // expected is a change worth noticing, not worth ignoring.
  for (const key of Object.keys(actual)) {
    if (!(key in expected)) {
      problems.push(`${path ? path + '.' : ''}${key}: unexpected field ${JSON.stringify(actual[key])}`);
    }
  }
  return problems;
}

module.exports = { loadDecoder, diff, VERSION_LINE };
