#!/usr/bin/env bash
# Builds and runs Tools/classify.cpp (PLAN.md §3.2): scans and classifies the Ableton library on
# this Mac and summarises it. Arguments go to the tool (--csv FILE).
#
#   Tools/classify.sh [--csv out.csv]
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD="$HERE/../Tests/build"
mkdir -p "$BUILD"
clang++ -std=c++17 -O2 -Wall -Wextra -I "$HERE/../Source" "$HERE/classify.cpp" -o "$BUILD/classify"
"$BUILD/classify" "$@"
