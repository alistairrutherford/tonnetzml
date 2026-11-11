#!/usr/bin/env bash
# Compiles and runs the TonnetzML core tests (PLAN.md §7.1). The core (Source/core) has no JUCE
# dependency, so this needs only Xcode's clang (no cmake, no brew, no plug-in build).
#
#   Tests/run_tests.sh
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC="$HERE/../Source"
BUILD="$HERE/build"
mkdir -p "$BUILD"
export TONNETZML_FIXTURES="$HERE/fixtures"

# On GitHub Actions, turn compiler errors into ::error annotations (readable
# through the public API, unlike the job log).
annotate_errors () {
    if [[ -n "${GITHUB_ACTIONS:-}" ]]; then
        grep -E "error:" "$1" | head -20 | sed 's/%/%25/g; s/^/::error title=Compile error::/'
    fi
}

compile () {
    local name="$1"
    if ! clang++ -std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter -Wshadow -Wconversion -Wno-sign-conversion \
        -I "$SRC" -I "$HERE" "$HERE/$name.cpp" -o "$BUILD/$name" 2> "$BUILD/$name.err"; then
        cat "$BUILD/$name.err" >&2; annotate_errors "$BUILD/$name.err"; exit 1
    fi
    cat "$BUILD/$name.err" >&2
}

compile_and_run () {
    local name="$1"
    echo "== building $name =="
    if ! clang++ -std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter -Wshadow -Wconversion -Wno-sign-conversion \
        -I "$SRC" -I "$HERE" "$HERE/$name.cpp" -o "$BUILD/$name" 2> "$BUILD/$name.err"; then
        cat "$BUILD/$name.err" >&2; annotate_errors "$BUILD/$name.err"; exit 1
    fi
    cat "$BUILD/$name.err" >&2
    "$BUILD/$name"
}

compile_and_run test_rig
compile_and_run test_lattice
compile_and_run test_ops
compile_and_run test_voicing
compile_and_run test_engine
compile_and_run test_orbit
compile_and_run test_keys
compile_and_run test_edge
compile_and_run test_editor
compile_and_run test_capture
compile_and_run test_preview

# The offline render for the Live checks (PLAN.md §7.4): built, and run once on a known scenario.
echo "== building render_scenario =="
compile render_scenario
printf '4 60 100 1\n4 64 100 1\n4 67 100 1\n' > "$BUILD/render_smoke.txt"
if "$BUILD/render_scenario" --gather 0 --word P "$BUILD/render_smoke.txt" | tr -d ' \n' \
        | grep -q '"notes":\[{"pitch":60,"start":4.000000,"length":1.000000,"velocity":100,"channel":2,"ppq":4.000000},{"pitch":63,'; then
    echo "  PASS  render_scenario: C E G under P renders as C Eb G on channel 2"
else
    echo "  FAIL  render_scenario: unexpected output" >&2
    "$BUILD/render_scenario" --gather 0 --word P "$BUILD/render_smoke.txt" >&2
    exit 1
fi

printf '4 60 100 16\n4 64 100 16\n4 67 100 16\n' > "$BUILD/render_orbit.txt"
if "$BUILD/render_scenario" --gather 0 --mode orbit --cycle PL "$BUILD/render_orbit.txt" | tr -d ' \n' \
        | grep -q '{"pitch":63,"start":8.000000,"length":12.000000,"velocity":100,"channel":2,"ppq":8.000000},{"pitch":68,"start":12.000000'; then
    echo "  PASS  render_scenario: Orbit PL on C E G steps on the bar lines (Cm at 8, Ab at 12)"
else
    echo "  FAIL  render_scenario: unexpected Orbit output" >&2
    "$BUILD/render_scenario" --gather 0 --mode orbit --cycle PL "$BUILD/render_orbit.txt" >&2
    exit 1
fi

echo ""
echo "All test binaries passed."
