#!/usr/bin/env bash
#
# Sets up the build toolchain and proves the sketch compiles.
# This is S01-00 (docs/sprints/sprint-01/s01-00-prove-the-project-still-builds.md).
#
# RUN AS YOUR NORMAL USER. Do NOT run this with sudo -- everything installs
# under $HOME. Root is only needed for USB flashing later; see
# scripts/setup-flashing-root.sh for that, and not until sprint 06.
#
# Idempotent: safe to re-run.
#
set -euo pipefail

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SKETCH="$REPO_DIR/adafruit-feather-m0-lora-ttn-2.ino"
BINDIR="$HOME/.local/bin"
FQBN="adafruit:samd:adafruit_feather_m0"
BOARD_URL="https://adafruit.github.io/arduino-board-index/package_adafruit_index.json"

say() { printf '\n\033[1m== %s\033[0m\n' "$*"; }
die() { printf '\n\033[31mFAILED: %s\033[0m\n' "$*" >&2; exit 1; }

[ "$(id -u)" -eq 0 ] && die "Do not run as root. Everything here installs under \$HOME."
command -v curl >/dev/null || die "curl not found. Install it first (needs root: apt install curl)."

say "1/7  arduino-cli"
export PATH="$BINDIR:$PATH"
if command -v arduino-cli >/dev/null; then
  echo "already installed: $(arduino-cli version)"
else
  mkdir -p "$BINDIR"
  curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | BINDIR="$BINDIR" sh \
    || die "arduino-cli install failed. Check egress to downloads.arduino.cc."
  command -v arduino-cli >/dev/null || die "arduino-cli not on PATH. Add $BINDIR to PATH and re-run."
fi
echo "PATH note: add this to your shell rc if it is not there already:"
echo "  export PATH=\"\$HOME/.local/bin:\$PATH\""

say "2/7  config + Adafruit board index"
arduino-cli config init --overwrite >/dev/null 2>&1 || true
arduino-cli config add board_manager.additional_urls "$BOARD_URL" >/dev/null 2>&1 \
  || arduino-cli config set board_manager.additional_urls "$BOARD_URL"
arduino-cli core update-index

say "3/7  Adafruit SAMD core (pulls the ARM toolchain; this is the slow one)"
arduino-cli core install adafruit:samd || die "core install failed"
arduino-cli core list

say "4/7  libraries"
# RTCZero is a hard dependency of ArduinoLowPower and is what actually owns the
# RTC -- see TODO #6. Install it explicitly so its version is recorded, not
# pulled in silently as a transitive dep.
for lib in \
  "MCCI LoRaWAN LMIC library" \
  "Arduino Low Power" \
  "RTCZero" \
  "OneWire" \
  "DallasTemperature"
do
  echo "--- $lib"
  arduino-cli lib install "$lib" || die "lib install failed: $lib"
done

say "5/7  RECORD THESE VERSIONS (S01-00 wants them pinned)"
arduino-cli lib list | grep -iE "LMIC|Low Power|RTCZero|OneWire|Dallas" || true
cat <<'NOTE'

  The DeviceTimeReq contract in TODO #6 was verified against MCCI LMIC v6.0.1:
    - LMIC_ENABLE_DeviceTimeReq defaults to 1 (src/lmic/config.h)
    - the callback is void cb(void*, int flagSuccess) and carries NO time;
      you must call LMIC_getNetworkTimeReference(&ref) inside it.
  If the version above differs, re-check that contract before sprint 03.
NOTE

say "6/7  lmic_project_config.h -- CFG_eu868"
CFG="$(find "$HOME/Arduino/libraries" -path '*MCCI*' -name 'lmic_project_config.h' 2>/dev/null | head -1)"
[ -n "$CFG" ] || die "lmic_project_config.h not found under ~/Arduino/libraries"
echo "found: $CFG"
if grep -qE '^\s*#define\s+CFG_eu868' "$CFG"; then
  echo "CFG_eu868 already defined"
else
  cp "$CFG" "$CFG.bak.$(date +%s)"
  # The stock file ships every region commented out. Enable eu868, leave the rest.
  sed -i 's|^//\s*#define CFG_eu868 1|#define CFG_eu868 1|' "$CFG"
  grep -qE '^\s*#define\s+CFG_eu868' "$CFG" || echo '#define CFG_eu868 1' >> "$CFG"
  echo "CFG_eu868 enabled (backup written alongside)"
fi
echo "--- effective region defines:"
grep -E '^\s*#define\s+CFG_' "$CFG" || true

# Commit a reference copy so the build config stops being folklore. It cannot
# live in its real location under version control -- that path is inside the
# library -- but it must be discoverable.
mkdir -p "$REPO_DIR/reference"
cp "$CFG" "$REPO_DIR/reference/lmic_project_config.h"
echo "reference copy -> reference/lmic_project_config.h  (commit this)"

say "7/7  compile"
[ -f "$SKETCH" ] || die "sketch not found: $SKETCH"
arduino-cli compile --fqbn "$FQBN" "$REPO_DIR" 2>&1 | tee /tmp/compile.log \
  || die "COMPILE FAILED -- stop and escalate. Everything downstream assumes this passes."

say "DONE -- record this baseline (S02-02 diffs against it)"
grep -iE "program storage|dynamic memory|bytes" /tmp/compile.log || tail -5 /tmp/compile.log

cat <<'NEXT'

Next:
  - Put the flash/RAM numbers above into a dev-note. That is the S02-02 baseline
    and there is no baseline today.
  - Commit reference/lmic_project_config.h.
  - Put the working commands into CLAUDE.md. If a second person cannot follow
    them to a green build, S01-00 is not done.
  - Sanity-check the guard: comment out CFG_eu868 and confirm the build #errors.
    That guard is load-bearing and should be proven, not assumed.

NEXT
