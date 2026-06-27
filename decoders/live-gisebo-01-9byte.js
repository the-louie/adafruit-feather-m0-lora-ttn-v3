function decodeUplink(input) {
  const data = {};
  const warnings = [];
  const errors = [];

  if (!input || input.bytes == null || input.bytes.length < 3) {
    errors.push("Missing input");
    return { data: {}, warnings, errors };
  }
  const bytes = input.bytes;

  const offset = (bytes[1] << 4) | (bytes[2] >> 4);
  data.battery_v = Number(((offset + 3000) / 1000).toFixed(3));
  data.sequence = bytes[2] & 0x0f;
  data.version = 5;

  const INTERVAL_MINUTES = [5, 1, 5, 15, 30, 60, 120, 360, 720, 1440, 10080];
  const intervalIndex = bytes[0] <= 10 ? bytes[0] : 0;
  const intervalSeconds = INTERVAL_MINUTES[intervalIndex] * 60;

  const anchorTimeMs = (input.recvTime ? new Date(input.recvTime) : new Date()).getTime();

  data.entries = [];

  for (let i = 3; i < bytes.length && i < 9; i++) {
    const v = bytes[i];
    if (v === 250) continue;

    const sensorIdx = i - 3;
    const offsetMs = sensorIdx * intervalSeconds * 1000;
    const entryTs = new Date(anchorTimeMs - offsetMs).toISOString();

    const entry = { timestamp: entryTs };

    if (v === 251) {
      entry.temperature_state = "too cold";
    } else if (v === 252) {
      entry.temperature_state = "too warm";
    } else if (v <= 200) {
      entry.temperature = Number(((v * 0.2) - 10).toFixed(1));
    }

    data.entries.push(entry);
  }

  return { data, warnings, errors };
}