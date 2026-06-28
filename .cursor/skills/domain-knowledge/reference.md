# Domain reference

## LMIC events (ev_t) & States

Common events used in this project:

| Event / State | Meaning |
|-------|---------|
| `EV_JOINING` | Join started. |
| `EV_JOINED` | Join success; `LMIC.devaddr` now set. Safe to set baseline TxPow/DR here. |
| `EV_JOIN_FAILED` | Join failed. Stack will natively retry based on duty cycle. |
| `EV_TXCOMPLETE` | Uplink finished (TX + RX1/RX2 windows done); safe to break synchronous wait and deep sleep after this. |
| `EV_TXSTART` (17) | Radio started transmitting. |
| `EV_JOIN_TXCOMPLETE` (20) | Join request TX done; waiting for Accept. |
| `OP_TXRXPEND` | *Opmode State (Not an event)*. Radio is busy. If true during `do_send`, abort send and wait. |

**Crucial:** Always wait synchronously for `EV_TXCOMPLETE` before calling `LowPower.deepSleep()`.

## Adafruit Feather M0 LoRa pin map

```c
const lmic_pinmap lmic_pins = {
    .nss = 8,
    .rxtx = LMIC_UNUSED_PIN,
    .rst = 4,
    .dio = {3, 6, LMIC_UNUSED_PIN}, // DIO1 must be physically jumpered to Pin 6
};