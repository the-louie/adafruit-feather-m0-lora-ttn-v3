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

// --- Diagnostic (error/health) frame, its own FPort, variant-independent. ---
// Mirrors diagnostics.h. FPort 1 = PROD, 2 = DEV. 11 bytes.
const DIAG_FPORT_PROD = 1;
const DIAG_FPORT_DEV = 2;
const DIAG_V1_LEN = 11;
const DIAG_V2_LEN = 16;   // schema 2 appends DS18B20 status, failure streak, ROM id

// Order matches diagnostics.h Ds18Status.
const DS18_STATUS_NAMES = [
  "ok", "not_found", "crc_or_no_response", "stuck_85", "out_of_range", "bus_ambiguous",
];

// Verbose DEV diagnostics ("all-clear" full-state snapshot), DEV-only, FPort 3.
// Schema 1 = 22 bytes; schema 2 (2026-07-29) appends uptime, cycle count,
// buffer state and the panel min/mean/max profile. Both decode -- old captures
// stay readable.
const VERBOSE_FPORT_DEV = 3;
const VERBOSE_V1_LEN = 22;
const VERBOSE_V2_LEN = 34;
const VERBOSE_V3_LEN = 37;   // schema 3 appends the 3-byte firmware git hash

// The sun EWMA's time constant (solar_signal.h: SUN_EWMA_TAU_S). clarity is
// sun_ewma normalised by expected daylight, and the EWMA needs roughly one
// time constant of history before that ratio means anything -- a device booted
// hours ago reports a low ratio that reads as a shaded panel (TODO 24b; the
// live fixture's clarity 0.006 at f_cnt=0 is this exact artefact, preserved
// as a reminder). Schema-2 frames carry uptime, so the gate is exact there;
// data frames carry no age field, so they stay ungated -- the backend, which
// has history, is the right place for that gate (TODO item 10).
const EWMA_TAU_S = 86400;
// Order matches season.h SeasonState: WINTER=0, MID=1, SUMMER=2.
const SEASON_NAMES = ["Winter", "Fall/Spring", "Summer"];

// Fault bitmap (bytes 5-6). Order matches diagnostics.h DIAG_FAULT_*.
const DIAG_FAULT_NAMES = [
  [0x0001, "ds18b20_not_found"],
  [0x0002, "ds18b20_bus_ambiguous"],
  [0x0004, "ds18b20_read_fail"],
  [0x0008, "ina219_read_fail"],
  [0x0010, "persist_corrupt"],
  [0x0020, "tx_timeout"],
  [0x0040, "low_battery"],
  [0x0080, "ina219_ovf"],
  [0x0100, "temp_implausible"],
];

// SAMD21 PM->RCAUSE bits (byte 2).
const RESET_CAUSE_NAMES = [
  [0x01, "power_on"],
  [0x02, "brownout_12"],
  [0x04, "brownout_33"],
  [0x10, "external"],
  [0x20, "watchdog"],
  [0x40, "system"],
];

function namesForBits(value, table) {
  const out = [];
  for (const [bit, name] of table) if (value & bit) out.push(name);
  return out;
}

function decodeDiagnostic(bytes, fPort) {
  const data = {}, warnings = [], errors = [];
  const schema = bytes.length >= 1 ? bytes[0] : 0;
  const expectedLen = schema >= 2 ? DIAG_V2_LEN : DIAG_V1_LEN;
  if (bytes.length !== expectedLen) {
    errors.push(`diagnostic FPort ${fPort} schema ${schema} expects ${expectedLen} bytes, got ${bytes.length}`);
    return { data: {}, warnings, errors };
  }
  if (schema < 1 || schema > 2) {
    warnings.push(`unknown diagnostic schema ${schema}; decoding as v${schema >= 2 ? 2 : 1}`);
  }

  const info = bytes[1];
  data.version = FIRMWARE_VERSION;
  data.frame = "diagnostic";
  data.diag_schema = schema;
  data.mode = (info & 0x01) ? "SOLAR" : "PRIMARY";
  data.run_mode = (fPort === DIAG_FPORT_DEV) ? "DEV" : "PROD";
  // The FPort and the info DEV bit encode the same thing; disagreement means a
  // misconfigured device or the wrong decoder.
  if (!!(info & 0x02) !== (fPort === DIAG_FPORT_DEV)) {
    warnings.push(`DEV bit (${!!(info & 0x02)}) disagrees with FPort ${fPort}`);
  }
  data.cold_boot = !!(info & 0x04);
  data.clock_valid = !!(info & 0x08);
  data.ina219_seen = !!(info & 0x10);

  data.reset_cause = bytes[2];
  data.reset_causes = namesForBits(bytes[2], RESET_CAUSE_NAMES);
  data.boot_counter = bytes[3];
  data.ds18b20_count = bytes[4];

  const faultBits = (bytes[5] << 8) | bytes[6];
  data.fault_bits = faultBits;
  data.faults = namesForBits(faultBits, DIAG_FAULT_NAMES);
  data.healthy = faultBits === 0;

  const probeCfg = (bytes[7] << 8) | bytes[8];
  data.ina219_config = "0x" + probeCfg.toString(16).toUpperCase().padStart(4, "0");
  data.ina219_config_ok = probeCfg === 0x399F;   // INA219 config-register reset value

  data.battery_v = Number((((bytes[9] << 8) | bytes[10]) / 1000).toFixed(3));

  if (schema >= 2) {
    data.ds18_status = DS18_STATUS_NAMES[bytes[11]] || `unknown_${bytes[11]}`;
    data.sensor_fail_streak = bytes[12];
    const rom = (bytes[13] << 16) | (bytes[14] << 8) | bytes[15];
    data.ds18_rom = rom === 0 ? null : rom.toString(16).padStart(6, "0");
  }

  return { data, warnings, errors };
}

// Verbose DEV snapshot (FPort 3). Mirrors diagnostics.h diagEncodeVerbose().
function decodeVerbose(bytes, recvTime) {
  const data = {}, warnings = [], errors = [];
  const schema = bytes.length >= 1 ? bytes[0] : 0;
  const expectedLen = schema >= 3 ? VERBOSE_V3_LEN
    : schema === 2 ? VERBOSE_V2_LEN : VERBOSE_V1_LEN;
  if (bytes.length !== expectedLen) {
    errors.push(`verbose FPort ${VERBOSE_FPORT_DEV} schema ${schema} expects ${expectedLen} bytes, got ${bytes.length}`);
    return { data: {}, warnings, errors };
  }
  // An unknown schema decodes as its nearest known layout, chosen by the same
  // rule as the length check above, and says which one it actually used.
  if (schema < 1 || schema > 3) {
    warnings.push(`unknown verbose schema ${schema}; decoding as v${schema >= 3 ? 3 : schema >= 2 ? 2 : 1}`);
  }
  const info = bytes[1];
  data.version = FIRMWARE_VERSION;
  data.frame = "verbose";
  data.diag_schema = schema;
  data.mode = (info & 0x01) ? "SOLAR" : "PRIMARY";
  data.run_mode = (info & 0x02) ? "DEV" : "PROD";
  data.cold_boot = !!(info & 0x04);
  data.clock_valid = !!(info & 0x08);
  data.ina219_seen = !!(info & 0x10);
  data.bonus_active = !!(info & 0x20);
  data.sensor_bus_ambiguous = !!(info & 0x40);

  data.reset_cause = bytes[2];
  data.reset_causes = namesForBits(bytes[2], RESET_CAUSE_NAMES);
  data.boot_counter = bytes[3];
  data.interval_index = bytes[4] <= 10 ? bytes[4] : 10;
  data.interval_minutes = INTERVAL_MINUTES[data.interval_index];
  data.season = SEASON_NAMES[bytes[5] & 0x03] || "unknown";
  data.voltage_offset = (bytes[5] >> 2) & 0x03;

  data.battery_v = Number((((bytes[6] << 8) | bytes[7]) / 1000).toFixed(3));
  data.panel_v = Number((((bytes[8] << 8) | bytes[9]) / 1000).toFixed(3));
  data.panel_ma = Number((((bytes[10] << 8) | bytes[11]) * 0.1).toFixed(1));
  data.sun_ewma = Number((bytes[12] / 255).toFixed(3));
  data.harvest_mah = (bytes[13] << 8) | bytes[14];

  const probeCfg = (bytes[15] << 8) | bytes[16];
  data.ina219_config = "0x" + probeCfg.toString(16).toUpperCase().padStart(4, "0");
  data.ina219_config_ok = probeCfg === 0x399F;
  data.ds18b20_count = bytes[17];

  const rawT = (bytes[18] << 8) | bytes[19];
  data.surface_temp = rawT === 0x7FFF ? null
    : Number(((rawT >= 0x8000 ? rawT - 0x10000 : rawT) / 100).toFixed(2));

  const faultBits = (bytes[20] << 8) | bytes[21];
  data.fault_bits = faultBits;
  data.faults = namesForBits(faultBits, DIAG_FAULT_NAMES);
  data.healthy = faultBits === 0;

  if (schema >= 2) {
    data.uptime_s = ((bytes[22] << 24) >>> 0) + (bytes[23] << 16) + (bytes[24] << 8) + bytes[25];
    data.uptime_h = Number((data.uptime_s / 3600).toFixed(2));
    data.cycle_count = (bytes[26] << 8) | bytes[27];
    data.ram_count = (bytes[28] >> 4) & 0x0F;
    data.uplink_counter = bytes[28] & 0x0F;

    // Panel profile since the previous verbose frame (min/mean/max), or nulls
    // when nothing was accumulated (primary variant, or the frame at boot).
    if (info & 0x80) {
      data.panel_ma_min  = Number((bytes[29] * 0.5).toFixed(1));
      data.panel_ma_mean = Number((bytes[30] * 0.5).toFixed(1));
      data.panel_ma_max  = Number((bytes[31] * 0.5).toFixed(1));
      data.panel_v_min   = Number((bytes[32] * 0.03).toFixed(2));
      data.panel_v_max   = Number((bytes[33] * 0.03).toFixed(2));
    } else {
      data.panel_ma_min = data.panel_ma_mean = data.panel_ma_max = null;
      data.panel_v_min = data.panel_v_max = null;
    }

    // Clarity with the convergence gate (TODO 24b): this frame carries both
    // the EWMA and its age, so the ratio is only emitted once the EWMA has
    // ~one time constant (24 h) of history. Uptime under-estimates the age
    // after a warm reset (the EWMA is restored, uptime is not), so the gate
    // errs toward suppressing -- conservative by design.
    if (recvTime) {
      const frac = expectedDaylightFraction(new Date(recvTime), SITE_LATITUDE_DEG);
      data.expected_daylight_fraction = Number(frac.toFixed(3));
      if (data.uptime_s >= EWMA_TAU_S) {
        data.clarity = frac > 0 ? Number((data.sun_ewma / frac).toFixed(3)) : null;
        data.clarity_converging = false;
      } else {
        data.clarity = null;
        data.clarity_converging = true;
      }
    } else {
      data.clarity = null;
    }
  }

  if (schema >= 3) {
    // The commit this firmware was built from (first 6 hex chars), injected at
    // build time by scripts/build.sh. 0 = unofficial build (compiled without
    // the script), reported as null so dashboards show the gap honestly.
    const h = (bytes[34] << 16) | (bytes[35] << 8) | bytes[36];
    data.fw_commit = h === 0 ? null : h.toString(16).padStart(6, "0");
  }

  return { data, warnings, errors };
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

  // Diagnostic frames ride their own FPort (1 PROD / 2 DEV) and are variant-
  // independent, so dispatch before the data-payload FPort/length checks.
  if (fPort === DIAG_FPORT_PROD || fPort === DIAG_FPORT_DEV) {
    return decodeDiagnostic(bytes, fPort);
  }
  if (fPort === VERBOSE_FPORT_DEV) {
    return decodeVerbose(bytes, input.recvTime);
  }

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
    // leaves / shade on the panel -- a fault a bare EWMA hides.
    //
    // Gated ONLY on recvTime, which the network server stamps on every uplink
    // and The Things Stack passes to the formatter as a Date. It is NOT gated on
    // data.clock_valid: neither operand touches the device's RTC.
    // expectedDaylightFraction() runs on the server timestamp plus a latitude
    // constant, and sun_ewma decays against sleepIntervalSeconds, not the clock.
    // Gating on the device clock only blanked clarity for a unit's whole early
    // life -- and forever on any unit whose DeviceTimeReq never landed -- which
    // is exactly when the "is something covering the panel?" signal is wanted.
    //
    // STILL MISSING (TODO 24b): clarity is meaningless until the EWMA has
    // converged. A device booted an hour ago reports a low ratio that reads as a
    // shaded panel. Guarding that needs the EWMA's age, which the payload does
    // not yet carry; expected_daylight_fraction is emitted alongside so the
    // backend can judge in the meantime.
    if (input.recvTime) {
      const frac = expectedDaylightFraction(new Date(input.recvTime), SITE_LATITUDE_DEG);
      data.expected_daylight_fraction = Number(frac.toFixed(3));
      data.clarity = frac > 0 ? Number((data.sun_ewma / frac).toFixed(3)) : null;
    } else {
      data.clarity = null;
    }
  }

  return { data, warnings, errors };
}
