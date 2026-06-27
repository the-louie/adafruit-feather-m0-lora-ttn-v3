#!/usr/bin/env bash
#
# The ONLY part of the toolchain that needs root: serial port access for
# flashing. Not needed until sprint 06, when hardware arrives.
#
# Building and compiling need NO root -- use scripts/setup-toolchain.sh as your
# normal user for that.
#
#   sudo ./scripts/setup-flashing-root.sh <your-username>
#
set -euo pipefail

[ "$(id -u)" -eq 0 ] || { echo "Run this one with sudo." >&2; exit 1; }
TARGET_USER="${1:-${SUDO_USER:-}}"
[ -n "$TARGET_USER" ] || { echo "Usage: sudo $0 <username>" >&2; exit 1; }
id "$TARGET_USER" >/dev/null 2>&1 || { echo "No such user: $TARGET_USER" >&2; exit 1; }

echo "== serial port access for $TARGET_USER"
# Debian/Ubuntu put USB serial devices in the dialout group. Without membership
# you get "Permission denied" on /dev/ttyACM0 and bossac cannot flash.
usermod -a -G dialout "$TARGET_USER"
echo "added $TARGET_USER to dialout"

# The Feather M0 bootloader enumerates as a different USB device than the
# running sketch (double-tap reset), so both need a rule.
cat > /etc/udev/rules.d/99-adafruit-feather-m0.rules <<'RULES'
# Adafruit Feather M0 -- sketch and bootloader (double-tap reset)
SUBSYSTEM=="usb", ATTRS{idVendor}=="239a", MODE="0664", GROUP="dialout"
SUBSYSTEM=="tty", ATTRS{idVendor}=="239a", MODE="0664", GROUP="dialout"
RULES
udevadm control --reload-rules && udevadm trigger
echo "udev rules installed and reloaded"

# curl is the only system dependency setup-toolchain.sh needs.
command -v curl >/dev/null || { echo "installing curl"; apt-get update -qq && apt-get install -y -qq curl; }

cat <<'NEXT'

Done.

  $TARGET_USER must log out and back in for dialout membership to take effect
  -- `newgrp dialout` works for the current shell only.

Verify once a board is plugged in:
  arduino-cli board list        # should show /dev/ttyACM0 as adafruit:samd:adafruit_feather_m0
  ls -l /dev/ttyACM0            # group should be dialout

Reminder for sprint 06: the idle(750) bug is PROD-only. Verifying it needs a
board strapped PROD (pin 11 floating, USB detached), which means no serial --
telemetry over LoRa. A DEV-strapped board will show nothing, which is exactly
how the bug survived for months.

NEXT
