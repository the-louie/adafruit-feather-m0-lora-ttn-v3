/**
 * TTN payload decoder for Protocol V5 (8-byte uplink).
 * Bytes 0-1: 12-bit battery offset from 3000 mV + 4-bit sequence.
 * Bytes 2-7: 6× temperature (0 = -10°C, 200 = +30°C; 250 = no value, 251 = too cold, 252 = too warm).
 * FPort 10 = PROD, 20 = DEV. For use in TTN dashboard payload formatter.
 */
function decodeUplink(input) {
  const data = {};
  const warnings = [];
  const errors = [];

  if (!input || input.bytes == null || input.bytes.length < 8) {
    errors.push("Missing input or payload length < 8");
    return { data: {}, warnings, errors };
  }
  const bytes = input.bytes;
  const fPort = input.fPort;

  const offset = (bytes[0] << 4) | (bytes[1] >> 4);
  data.battery_voltage = Number(((offset + 3000) / 1000).toFixed(3));
  data.sequence = bytes[1] & 0x0f;
  data.rebootDetected = (data.sequence === 0);
  data.version = 5;
  data.mode = (fPort === 10) ? "PROD_V5" : (fPort === 20) ? "DEV_V5" : "UNKNOWN";

  data.temperatures = [];
  for (let i = 2; i < 8; i++) {
    const v = bytes[i];

    // Only push to the array if it's NOT a null value (250)
    if (v === 251) data.temperatures.push("< -10");
    else if (v === 252) data.temperatures.push("> +30");
    else if (v <= 200) {
      data.temperatures.push(Number(((v * 0.2) - 10).toFixed(1)));
    }
    // Note: v === 250 is simply ignored now, filtering the nulls.
  }

  return { data, warnings, errors };
}
