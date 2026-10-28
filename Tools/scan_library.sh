#!/usr/bin/env bash
# Builds and runs Tools/scan_library.cpp (spike d, PLAN.md §6): times a scan of the Ableton
# library on this Mac and summarises what it finds. Arguments go to the tool (--csv FILE).
#
#   Tools/scan_library.sh [--csv out.csv]
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD="$HERE/../Tests/build"
mkdir -p "$BUILD"
clang++ -std=c++17 -O2 -Wall -Wextra -I "$HERE/../Source" "$HERE/scan_library.cpp" -o "$BUILD/scan_library"
"$BUILD/scan_library" "$@"
