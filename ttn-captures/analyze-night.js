// Overnight analysis of gisebo-05, 2026-07-27/28.
//
// Decodes every captured frame with the REPO decoder (decoders/gisebo-05-v7.js),
// not with whatever TTN's decoded_payload says -- the uploaded formatter may be
// older than the repo (the season-name fix 8dc181f landed after the flash).
// Every disagreement between the two is reported, because a disagreement means
// the TTN formatter needs re-uploading.
//
// Usage: node ttn-captures/analyze-night.js <capture.jsonl>

const fs = require('fs');
const path = require('path');
const { loadDecoder } = require('../test/harness.js');

const CAP = process.argv[2] ||
  path.join(__dirname, 'gisebo05-ttn-20260728-0853Z-last24h.jsonl');

const decode = loadDecoder(path.join(__dirname, '..', 'decoders', 'gisebo-05-v7.js'));

const rows = fs.readFileSync(CAP, 'utf8').split('\n').filter(l => l.trim())
  .map(l => JSON.parse(l).result);

const cest = ts => new Date(ts).toLocaleString('sv-SE',
  { timeZone: 'Europe/Stockholm' }).slice(5, 19);

// --- Decode everything ourselves -------------------------------------------
const frames = rows.map((r, i) => {
  const u = r.uplink_message;
  const bytes = Array.from(Buffer.from(u.frm_payload || '', 'base64'));
  const meta = (u.rx_metadata || [])[0] || {};
  let mine = null, err = null;
  try {
    mine = decode({ bytes, fPort: u.f_port, recvTime: u.received_at });
  } catch (e) { err = e.message; }
  return {
    i,
    at: u.received_at,
    t: cest(u.received_at),
    // Distinguish "absent" from "zero": TTN omits zero-valued fields.
    fPortRaw: Object.prototype.hasOwnProperty.call(u, 'f_port') ? u.f_port : undefined,
    fCntRaw: Object.prototype.hasOwnProperty.call(u, 'f_cnt') ? u.f_cnt : undefined,
    len: bytes.length,
    hex: Buffer.from(bytes).toString('hex'),
    ttn: u.decoded_payload || null,
    mine: mine ? mine.data : null,
    warnings: mine ? mine.warnings : [],
    errors: mine ? (mine.errors || []) : [err],
    rssi: meta.rssi, snr: meta.snr,
    gw: (meta.gateway_ids || {}).gateway_id,
    sf: ((u.settings || {}).data_rate || {}).lora
      ? u.settings.data_rate.lora.spreading_factor : null,
    freq: (u.settings || {}).frequency,
    airtime: u.consumed_airtime,
    gwTime: meta.time || meta.gps_time || null,
  };
});

// --- 1. Raw field presence --------------------------------------------------
console.log('=== 1. RAW FIELD PRESENCE (does "missing" mean zero?) ===');
const noPort = frames.filter(f => f.fPortRaw === undefined);
const noCnt = frames.filter(f => f.fCntRaw === undefined);
console.log(`frames total          : ${frames.length}`);
console.log(`f_port key absent     : ${noPort.length}  -> indices ${noPort.map(f => f.i).join(',')}`);
console.log(`f_cnt  key absent     : ${noCnt.length}  -> indices ${noCnt.map(f => f.i).join(',')}`);
console.log(`payload len 0 frames  : ${frames.filter(f => f.len === 0).length}`);
console.log('port histogram        :', JSON.stringify(
  frames.reduce((m, f) => (m[String(f.fPortRaw)] = (m[String(f.fPortRaw)] || 0) + 1, m), {})));

// --- 2. Timeline ------------------------------------------------------------
console.log('\n=== 2. TIMELINE (repo decoder) ===');
const hdr = ['#', 'time CEST', 'port', 'fcnt', 'len', 'kind', 'upc', 'boot',
  'batt', 'panelV', 'pmA', 'ewma', 'mAh', 'iv', 'season', 'flags', 'temps',
  'rssi', 'snr', 'sf'];
const line = [];
for (const f of frames) {
  const d = f.mine || {};
  const kind = f.fPortRaw === 21 ? 'DATA-SOL'
    : f.fPortRaw === 20 ? 'DATA-PRI'
      : f.fPortRaw === 3 ? 'VERBOSE'
        : f.fPortRaw === 2 ? 'FAULT'
          : f.fPortRaw === undefined ? 'NO-PORT' : `port${f.fPortRaw}`;
  const flags = [
    d.cold_boot ? 'CB' : '', d.soft_reset ? 'SR' : '',
    d.clock_valid ? 'CV' : '', d.bonus_active ? 'BON' : '',
    d.tx_timeout ? 'TXTO' : '',
    (d.faults && d.faults.length) ? 'F:' + d.faults.join('|') : '',
    d.reset_cause ? 'rc=' + d.reset_cause : '',
  ].filter(Boolean).join(' ');
  line.push([
    f.i, f.t, f.fPortRaw === undefined ? '--' : f.fPortRaw,
    f.fCntRaw === undefined ? '--' : f.fCntRaw, f.len, kind,
    d.uplink_counter ?? '', d.boot_counter ?? '',
    d.battery_v ?? '', d.panel_v ?? '', d.panel_ma ?? '',
    d.sun_ewma ?? '', d.harvest_mah ?? '',
    d.interval_minutes ?? '', d.season ?? '', flags,
    (d.entries || []).map(e => e.temperature).join(','),
    f.rssi ?? '', f.snr ?? '', f.sf ?? '',
  ].map(String));
}
const w = hdr.map((h, c) => Math.max(h.length, ...line.map(r => r[c].length)));
const fmt = r => r.map((v, c) => v.padEnd(w[c])).join(' ');
console.log(fmt(hdr));
line.forEach(r => console.log(fmt(r)));

// --- 3. Repo decoder vs TTN formatter --------------------------------------
console.log('\n=== 3. REPO DECODER vs TTN FORMATTER (disagreements) ===');
let disagree = 0;
for (const f of frames) {
  if (!f.ttn || !f.mine) {
    console.log(`#${f.i} ${f.t} port=${f.fPortRaw} len=${f.len}  ` +
      `ttn=${f.ttn ? 'decoded' : 'NOT DECODED'} repo=${f.mine ? 'decoded' : 'FAILED'}` +
      (f.errors.length ? ` errors=${JSON.stringify(f.errors)}` : ''));
    disagree++;
    continue;
  }
  const keys = new Set([...Object.keys(f.ttn), ...Object.keys(f.mine)]);
  const diffs = [];
  for (const k of keys) {
    const a = JSON.stringify(f.ttn[k]), b = JSON.stringify(f.mine[k]);
    if (a !== b) diffs.push(`${k}: ttn=${a} repo=${b}`);
  }
  if (diffs.length) {
    console.log(`#${f.i} ${f.t} port=${f.fPortRaw}: ${diffs.join(' | ')}`);
    disagree++;
  }
}
if (!disagree) console.log('none — uploaded formatter matches the repo decoder.');

// --- 4. Gaps ----------------------------------------------------------------
console.log('\n=== 4. INTER-FRAME GAPS ===');
for (let i = 1; i < frames.length; i++) {
  const min = (new Date(frames[i].at) - new Date(frames[i - 1].at)) / 60000;
  const mark = min > 70 ? '  <-- LONG' : '';
  console.log(`${frames[i - 1].t} -> ${frames[i].t}  ${min.toFixed(1).padStart(7)} min` +
    `  (#${frames[i - 1].i}->#${frames[i].i})${mark}`);
}
const last = frames[frames.length - 1];
console.log(`\nlast frame ${last.t} CEST; silence since then: ` +
  `${((Date.now() - new Date(last.at)) / 60000).toFixed(0)} min (as of run time)`);

// --- 5. f_cnt continuity ----------------------------------------------------
console.log('\n=== 5. f_cnt SEQUENCE (session restarts) ===');
let prev = null;
for (const f of frames) {
  const c = f.fCntRaw === undefined ? 0 : f.fCntRaw;
  let note = '';
  if (prev !== null) {
    if (c === 0 && prev !== 0) note = '  *** f_cnt RESET -> new session / rejoin';
    else if (c <= prev) note = `  *** NON-MONOTONIC (prev ${prev})`;
    else if (c > prev + 1) note = `  gap of ${c - prev - 1} frame(s) not stored`;
  }
  console.log(`#${String(f.i).padStart(2)} ${f.t}  f_cnt=${String(c).padStart(3)}` +
    ` port=${String(f.fPortRaw ?? '--').padStart(3)}${note}`);
  prev = c;
}

// --- 6. Gateway clock skew --------------------------------------------------
console.log('\n=== 6. GATEWAY TIME vs NETWORK received_at ===');
for (const f of frames) {
  if (!f.gwTime) { console.log(`#${f.i} ${f.t}  gw time absent`); continue; }
  const skew = (new Date(f.gwTime) - new Date(f.at)) / 60000;
  console.log(`#${String(f.i).padStart(2)} ${f.t}  skew ${skew.toFixed(2).padStart(8)} min` +
    `  gw=${f.gw} rssi=${f.rssi} snr=${f.snr}`);
}
