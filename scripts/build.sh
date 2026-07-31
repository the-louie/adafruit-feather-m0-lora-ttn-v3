#!/usr/bin/env bash
#
# Release build: tests -> clean-tree check -> compile with the commit hash
# baked into the verbose frame (schema 3, bytes 34-36).
#
#   scripts/build.sh              release: refuses a dirty tree, stamps HEAD's hash
#   scripts/build.sh --dev        iteration: tests still run, tree may be dirty,
#                                 hash is 0x000000 (decoder reports fw_commit
#                                 null, the "unofficial build" marker)
#   scripts/build.sh --fixed-keys ONE-OFF for a legacy device whose TTN
#                                 registration predates derived credentials.
#                                 Compiles fixed_keys.h (gitignored, holds a
#                                 root key) in place of the derived identity.
#                                 The image is device-specific: its name carries
#                                 the DevEUI and it must never be flashed to
#                                 another board.
#
# Why the hash is injected at compile time and never committed: a committed
# file cannot contain the hash of the commit that contains it. The flag goes
# through compiler.cpp.extra_flags, which is EMPTY at platform level -- do NOT
# move it to build.extra_flags, which the board definition needs for
# __SAMD21G18A__ and the USB flags. --clean guarantees the flag change cannot
# be masked by a cached object file.
#
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

MODE=release
[ "${1:-}" = "--dev" ] && MODE=dev
[ "${1:-}" = "--fixed-keys" ] && MODE=fixedkeys

EXTRA=""
LABEL=""
if [ "$MODE" = "fixedkeys" ]; then
  [ -f fixed_keys.h ] || { echo "ERROR: --fixed-keys needs fixed_keys.h (gitignored)"; exit 1; }
  EXTRA=" -DFW_FIXED_KEYS"
  LABEL="$(grep -oP 'FIXED_DEVEUI_MSB\[8\] = \{ \K[^}]*' fixed_keys.h | sed 's/0x//g; s/[ ,]//g' | tr 'a-f' 'A-F')"
  echo "FIXED-KEYS build for DevEUI ${LABEL} -- device-specific, do not reuse"
fi

FQBN=adafruit:samd:adafruit_feather_m0
OUT=build

echo "== 1/4 host tests =="
(cd test/host && ./run_tests.sh)

echo "== 2/4 decoder tests =="
node test/run.js
node test/run_v7.js

echo "== 3/4 source state =="
DIRTY="$(git status --porcelain)"
if [ -n "$DIRTY" ]; then
  if [ "$MODE" != "dev" ]; then
    echo "$DIRTY"
    echo ""
    echo "ERROR: working tree is not committed. The embedded hash must name a"
    echo "commit that contains exactly this source, so a release build refuses"
    echo "a dirty tree. Commit first (small batches, per CLAUDE.md), or use"
    echo "  scripts/build.sh --dev"
    echo "for an iteration build stamped as unofficial (fw_commit null)."
    exit 1
  fi
  HASH=000000
  echo "dev build on a dirty tree -> hash 0x000000 (unofficial)"
else
  HASH="$(git rev-parse HEAD | cut -c1-6)"
  echo "clean tree at $(git rev-parse --short HEAD) -> embedding 0x${HASH}"
fi

echo "== 4/4 compile =="
arduino-cli compile --clean --fqbn "$FQBN" \
  --build-property "compiler.cpp.extra_flags=-DFW_GIT_HASH24=0x${HASH}${EXTRA}" \
  --output-dir "$OUT" .

BIN="$OUT/adafruit-feather-m0-lora-ttn-2.ino.bin"
if [ "$MODE" = "fixedkeys" ]; then
  HANDOFF="FIXEDKEYS-${LABEL}-fw-${HASH}.ino.bin"
  cp "$BIN" "$HANDOFF"
  echo ""
  echo "device-specific image : $HANDOFF"
  echo "  ^ pinned to DevEUI ${LABEL}. Flashing this to any other board will"
  echo "    put two devices on the same LoRaWAN identity. Delete after use."
elif [ "$MODE" = "release" ]; then
  HANDOFF="gisebo-05-fw-${HASH}.ino.bin"
  cp "$BIN" "$HANDOFF"
  echo ""
  echo "release image : $HANDOFF"
else
  HANDOFF="$BIN"
fi
echo "commit        : ${HASH} ($( [ "$HASH" = "000000" ] && echo 'unofficial' || git log -1 --format=%s ))"
echo "md5           : $(md5sum "$HANDOFF" | cut -d' ' -f1)"
echo "size          : $(stat -c%s "$HANDOFF") bytes"
echo ""
echo "flash: double-tap RESET, then"
echo "  bossac -i -d --port=<port> -U true -i -e -w -v -R --offset=0x2000 $HANDOFF"
