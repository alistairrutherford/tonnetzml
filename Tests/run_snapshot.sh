#!/usr/bin/env bash
# The editor snapshot and host test (PLAN.md §7.2): builds Tests/Snapshot (a console app with the
# real processor and editor), renders the editor and compares it with Tests/fixtures/snapshot,
# and checks MIDI through, the test rig, real-time safety and state (Tests/Snapshot/Main.cpp).
#
#   Tests/run_snapshot.sh            # compare
#   Tests/run_snapshot.sh --update   # write new reference images (after a deliberate UI change)
#
# The images of each run are in Tests/build/snapshot.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJUCER="${PROJUCER:-$HOME/Development/JUCE/extras/Projucer/Builds/MacOSX/build/Release/Projucer.app/Contents/MacOS/Projucer}"

"$PROJUCER" --resave "$HERE/Snapshot/TonnetzMLSnapshot.jucer" > /dev/null
set -o pipefail
xcodebuild -project "$HERE/Snapshot/Builds/MacOSX/TonnetzMLSnapshot.xcodeproj" -configuration Release build 2>&1 \
    | grep -E "error:|BUILD (SUCCEEDED|FAILED)" | sort -u

cd "$HERE"
"$HERE/Snapshot/Builds/MacOSX/build/Release/TonnetzMLSnapshot" fixtures/snapshot build/snapshot "$@"
