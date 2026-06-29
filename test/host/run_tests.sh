#!/usr/bin/env bash
# Compiles and runs the host-side firmware logic tests.
#
# These test the SAME headers the sketch includes -- not copies. Anything that
# needs Arduino APIs cannot be tested here and belongs on a bench (sprint 06).
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
mkdir -p /tmp/hosttests
fail=0
for src in test_*.cpp; do
  bin="/tmp/hosttests/${src%.cpp}"
  g++ -std=c++17 -Wall -Wextra -Werror -I../.. -o "$bin" "$src"
  "$bin" || fail=1
done
exit $fail
