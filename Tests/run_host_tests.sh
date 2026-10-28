#!/usr/bin/env bash
# The processor tests (PLAN.md §7): builds Tests/Host (a console app with the processor and the
# editor) and runs it. Needs Xcode and JUCE 8.0.14 at ~/Development/JUCE (the module path in
# DrumChimpHost.jucer), with Projucer built (Tools/install.sh builds it once).
#
#   Tests/run_host_tests.sh                     everything, then again as a Mac with no library
#   Tests/run_host_tests.sh --update-snapshot   write a new reference image (after a deliberate UI change)
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJUCER="${PROJUCER:-$HOME/Development/JUCE/extras/Projucer/Builds/MacOSX/build/Release/Projucer.app/Contents/MacOS/Projucer}"

"$PROJUCER" --resave "$HERE/Host/DrumChimpHost.jucer" > /dev/null
mkdir -p "$HERE/build"
if ! xcodebuild -project "$HERE/Host/Builds/MacOSX/DrumChimpHost.xcodeproj" -configuration Release build > "$HERE/build/host-build.log" 2>&1; then
    grep -E "error:" "$HERE/build/host-build.log" | sort -u | head -20 >&2
    echo "the build failed (see $HERE/build/host-build.log)" >&2
    exit 1
fi
"$HERE/Host/Builds/MacOSX/build/Release/DrumChimpHost" "$@"
if [[ " $* " != *" --update-snapshot "* ]]; then
    DRUMCHIMP_LIBRARY=none "$HERE/Host/Builds/MacOSX/build/Release/DrumChimpHost" --no-library
fi
