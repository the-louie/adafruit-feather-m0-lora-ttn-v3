### Test Vector 1: Standard Production Data

**Scenario:** 5.8V Battery, Sequence 5, PROD mode, descending temperatures from 15°C to 14°C.

* **Hex Payload:** `11 85 7D 7B 7A 78 77 75`
* **FPort:** `10`

**Expected Result:**

```json
{
  "batteryVoltage": 5.848,
  "sequence": 5,
  "mode": "PROD",
  "rebootDetected": false,
  "temperatures": [15.0, 14.6, 14.4, 14.0, 13.8, 13.4]
}

```

### Test Vector 2: Reboot & Extreme Cold

**Scenario:** 6.1V Battery, Sequence 0 (Reboot!), DEV mode, one sensor read at -10.2°C (Too Cold).

* **Hex Payload:** `13 60 FB FA FA FA FA FA`
* **FPort:** `20`

**Expected Result:**

```json
{
  "batteryVoltage": 6.112,
  "sequence": 0,
  "mode": "DEV",
  "rebootDetected": true,
  "temperatures": ["< -10", null, null, null, null, null]
}

```
