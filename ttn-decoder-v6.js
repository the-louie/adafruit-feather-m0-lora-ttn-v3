/**
 * TTN payload decoder for Protocol (9-byte uplink with dynamic interval).
 * Byte 0: interval index (0–10). Bytes 1–2: 12-bit battery offset from 3000 mV + 4-bit sequence.
 * Bytes 3–8: 6× temperature (0 = -10°C, 200 = +30°C; 250 = no value, 251 = too cold, 252 = too warm).
 * FPort 10 = PROD, 20 = DEV. For use in TTN dashboard payload formatter.
 */
const INTERVAL_MINUTES_BY_INDEX = [null, 1, 5, 15, 30, 60, 120, 360, 720, 1440, 10080];
const INTERVAL_LABEL_BY_INDEX = ["unused", "1m", "5m", "15m", "30m", "1h", "2h", "6h", "12h", "24h", "7d"];

function decodeUplink(input) {
  const data = {};
  const warnings = [];
  const errors = [];

  if (!input || input.bytes == null || input.bytes.length < 9) {
    errors.push("Missing input or payload length < 9");
    return { data: {}, warnings, errors };
  }
  const bytes = input.bytes;
  const fPort = input.fPort;

  const rawByte = bytes[0] | 0;
  const intervalIndex = rawByte < 0 ? 0 : (rawByte > 10 ? 10 : rawByte);
  data.interval_index = intervalIndex;
  data.interval_minutes = INTERVAL_MINUTES_BY_INDEX[intervalIndex] ?? null;
  data.interval_label = INTERVAL_LABEL_BY_INDEX[intervalIndex] ?? "unknown";

  const offset = (bytes[1] << 4) | (bytes[2] >> 4);
  data.battery_v = Number(((offset + 3000) / 1000).toFixed(3));
  data.sequence = bytes[2] & 0x0f;
  data.rebootDetected = (data.sequence === 0);
  data.version = 6;
  data.mode = (fPort === 10) ? "PROD_V6" : (fPort === 20) ? "DEV_V6" : "UNKNOWN";

  data.temperatures = [];
  for (let i = 3; i < 9; i++) {
    const v = bytes[i];

    if (v === 250) data.temperatures.push(null);
    else if (v === 251) data.temperatures.push("< -10");
    else if (v === 252) data.temperatures.push("> +30");
    else if (v <= 200) {
      data.temperatures.push(Number(((v * 0.2) - 10).toFixed(1)));
    }
  }

  return { data, warnings, errors };
}
