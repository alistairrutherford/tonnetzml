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

echo ""
echo "All test binaries passed."
