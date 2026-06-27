function decodeUplink(input) {
  const data = {};
  const warnings = [];
  const errors = [];

  if (!input || input.bytes == null || input.bytes.length < 8) {
    errors.push("Missing input or payload length under 8");
    return { data: {}, warnings, errors };
  }
  const bytes = input.bytes;

  const offset = (bytes[0] << 4) | (bytes[1] >> 4);
  data.battery_v = Number(((offset + 3000) / 1000).toFixed(3));
  data.sequence = bytes[1] & 0x0f;
  data.version = 5.0;

  const anchorTimeMs = new Date(input.recvTime || Date.now()).getTime();
  const intervalSeconds = 300;

  data.entries = [];

  for (let i = 2; i < 8; i++) {
    const v = bytes[i];
    if (v === 250) continue;

    const sensorIdx = i - 2;
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