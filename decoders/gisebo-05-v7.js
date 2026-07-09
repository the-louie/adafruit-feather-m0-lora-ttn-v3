// TTN payload decoder for gisebo-05 (protocol v7).
//
// Handles both v7 shapes:
//   9 bytes,  FPort 10/20  -> primary variant
//   15 bytes, FPort 11/21  -> solar variant (adds panel V/I, EWMA, harvest, status)
//
// Bytes 0-8 are identical between them, so the temperature/battery output matches
// gisebo-01's schema (battery_v, entries[]) and the existing telegraf pipeline
// keeps working. What is new: uplink_counter (not sequence), a derived version,
// interval_index/interval_minutes (byte 0, which no prior decoder ever emitted),
// and the solar fields.

// --- Per-device constants. Set these to match the unit this decoder serves. ---
const FIRMWARE_VERSION = 7;
const SITE_LATITUDE_DEG = 57.81;   // for day-length clarity; a degree or two is minutes

const INTERVAL_MINUTES = [null, 1, 5, 15, 30, 60, 120, 360, 720, 1440, 10080];

// Expected fraction of the day that is daylight, at a given date and latitude.
// Standard solar-geometry approximation. Used only to normalise the sun EWMA
// into a "clarity" figure -- it never touches the device; it is pure backend.
function expectedDaylightFraction(date, latDeg) {
  const dayOfYear = Math.floor(
    (Date.UTC(date.getUTCFullYear(), date.getUTCMonth(), date.getUTCDate()) -
     Date.UTC(date.getUTCFullYear(), 0, 0)) / 86400000);
  const lat = (latDeg * Math.PI) / 180;
  // Solar declination (radians).
  const decl = 0.4093 * Math.sin((2 * Math.PI * (dayOfYear - 81)) / 365);
  // Hour angle of sunrise. cosH clamps at the poles (polar day/night).
  let cosH = -Math.tan(lat) * Math.tan(decl);
  if (cosH > 1) return 0;    // polar night
  if (cosH < -1) return 1;   // polar day
  const H = Math.acos(cosH);
  return H / Math.PI;        // fraction of 24 h that is daylight
}

function decodeTempSlot(v) {
  if (v === 250) return { skip: true };
  if (v === 251) return { temperature_state: "too cold" };
  if (v === 252) return { temperature_state: "too warm" };
  if (v <= 200) return { temperature: Number(((v * 0.2) - 10).toFixed(1)) };
  // 201-249 and 253-255 are out of contract; report explicitly rather than
  // silently dropping (S05-20 decision).
  return { temperature_state: "out of range", raw: v };
}

function decodeUplink(input) {
  const data = {};
  const warnings = [];
  const errors = [];

  if (!input || input.bytes == null) {
    errors.push("Missing input");
    return { data: {}, warnings, errors };
  }
  const bytes = input.bytes;
  const fPort = input.fPort;
  const len = bytes.length;

  const isSolar = (fPort === 11 || fPort === 21);
  const isPrimary = (fPort === 10 || fPort === 20);

  // Length must match the FPort. A mismatch means a misconfigured device or the
  // wrong decoder -- error loudly rather than parse garbage.
  if (isSolar && len !== 15) {
    errors.push(`solar FPort ${fPort} expects 15 bytes, got ${len}`);
    return { data: {}, warnings, errors };
  }
  if (isPrimary && len !== 9) {
    errors.push(`primary FPort ${fPort} expects 9 bytes, got ${len}`);
    return { data: {}, warnings, errors };
  }
  if (!isSolar && !isPrimary) {
    errors.push(`unexpected FPort ${fPort} for a v7 device`);
    return { data: {}, warnings, errors };
  }

  data.version = FIRMWARE_VERSION;
  data.mode = isSolar ? "SOLAR" : "PRIMARY";

  // Byte 0: interval index. Emitted for the FIRST TIME -- no prior decoder
  // reported it, so the backend never saw what the interval algorithm decided.
  const idx = bytes[0] <= 10 ? bytes[0] : 10;
  data.interval_index = idx;
  data.interval_minutes = INTERVAL_MINUTES[idx];

  // Bytes 1-2: battery offset + uplink counter.
  const offset = (bytes[1] << 4) | (bytes[2] >> 4);
  data.battery_v = Number(((offset + 3000) / 1000).toFixed(3));

  // The 4-bit field is an UPLINK COUNTER at v7 (a wake counter at v6). It steps
  // by 1 per successful uplink: a gap means a dropped message, a repeat means a
  // TX retry. NOT reboot detection -- that lives in the status byte (solar) or
  // in the TTN f_cnt metadata (both).
  data.uplink_counter = bytes[2] & 0x0f;

  // Bytes 3-8: six temperatures, newest first, with extrapolated timestamps.
  // sensorIdx comes from BYTE POSITION, so a skipped (null) slot does not shift
  // the timestamps of the others.
  const intervalSeconds = (INTERVAL_MINUTES[idx] || 5) * 60;
  const anchorMs = (input.recvTime ? new Date(input.recvTime) : new Date()).getTime();
  data.entries = [];
  for (let i = 3; i < 9; i++) {
    const slot = decodeTempSlot(bytes[i]);
    if (slot.skip) continue;
    const sensorIdx = i - 3;
    const ts = new Date(anchorMs - sensorIdx * intervalSeconds * 1000).toISOString();
    data.entries.push(Object.assign({ timestamp: ts }, slot));
  }

  if (isSolar) {
    // Byte 9: panel bus voltage, 30 mV/LSB.
    data.panel_v = Number(((bytes[9] * 30) / 1000).toFixed(3));
    // Byte 10: panel current, 0.5 mA/LSB.
    data.panel_ma = Number((bytes[10] * 0.5).toFixed(1));
    // Byte 11: sun-presence EWMA, 0-255 -> 0.0-1.0.
    data.sun_ewma = Number((bytes[11] / 255).toFixed(3));
    // Bytes 12-13: harvest accumulator, big-endian, 1 mAh/LSB (wraps).
    data.harvest_mah = (bytes[12] << 8) | bytes[13];
    // Byte 14: status. High 3 bits boot counter, low 5 flags.
    const status = bytes[14];
    data.boot_counter = (status >> 5) & 0x07;
    data.cold_boot = !!(status & 0x01);
    data.soft_reset = !!(status & 0x02);
    data.clock_valid = !!(status & 0x04);
    data.bonus_active = !!(status & 0x08);
    data.tx_timeout = !!(status & 0x10);

    // Clarity: EWMA normalised by how much daylight there SHOULD be. ~1.0 = clear
    // skies; persistently low against a high expectation = overcast, or snow /
    // leaves / shade on the panel -- a fault a bare EWMA hides. Only meaningful
    // with a valid clock; null otherwise.
    if (data.clock_valid && input.recvTime) {
      const frac = expectedDaylightFraction(new Date(input.recvTime), SITE_LATITUDE_DEG);
      data.expected_daylight_fraction = Number(frac.toFixed(3));
      data.clarity = frac > 0 ? Number((data.sun_ewma / frac).toFixed(3)) : null;
    } else {
      data.clarity = null;
    }
  }

  return { data, warnings, errors };
}
