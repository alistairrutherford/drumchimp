#!/usr/bin/env bash
# Compiles and runs the DrumChimp core tests. The core (Source/core) has no JUCE dependency, so
# this needs only Xcode's clang (no cmake, no brew, no plug-in build).
#
#   Tests/run_tests.sh
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC="$HERE/../Source"
BUILD="$HERE/build"
mkdir -p "$BUILD"
export DRUMCHIMP_FIXTURES="$HERE/fixtures"

compile_and_run () {
    local name="$1"
    echo "== building $name =="
    if ! clang++ -std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter -Wshadow -Wconversion -Wno-sign-conversion \
        -I "$SRC" -I "$HERE" "$HERE/$name.cpp" -o "$BUILD/$name" 2> "$BUILD/$name.err"; then
        cat "$BUILD/$name.err" >&2; exit 1
    fi
    cat "$BUILD/$name.err" >&2
    "$BUILD/$name"
}

compile_and_run test_core
compile_and_run test_sampler
compile_and_run test_classifier
compile_and_run test_library
compile_and_run test_kit
compile_and_run test_pattern
