#!/usr/bin/env bash
# Host validation (PLAN.md §7.2): pluginval at strictness 10 for the VST3.
#
# Needs the installed plug-in (Tools/install.sh), in ~/Library/Audio/Plug-Ins/VST3. pluginval is found as $PLUGINVAL, on PATH, or in /Applications or
# ~/Applications.
#
#   Tests/run_validation.sh
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VST3="$HOME/Library/Audio/Plug-Ins/VST3/TonnetzML.vst3"
STRICTNESS="${PLUGINVAL_STRICTNESS:-10}"

[[ -d "$VST3" ]] || { echo "Missing $VST3: run Tools/install.sh first." >&2; exit 2; }

PV="${PLUGINVAL:-}"
if [[ -z "$PV" ]]; then
    for c in "$(command -v pluginval || true)" /Applications/pluginval.app/Contents/MacOS/pluginval \
             "$HOME/Applications/pluginval.app/Contents/MacOS/pluginval"; do
        [[ -n "$c" && -x "$c" ]] && { PV="$c"; break; }
    done
fi
[[ -n "$PV" ]] || { echo "pluginval not found (set PLUGINVAL)." >&2; exit 2; }

mkdir -p "$HERE/build/pluginval"
echo "== pluginval VST3 (strictness $STRICTNESS) =="
if "$PV" --strictness-level "$STRICTNESS" --timeout-ms 600000 --validate-in-process \
         --output-dir "$HERE/build/pluginval" --validate "$VST3"; then
    echo "-- pluginval VST3 (strictness $STRICTNESS): PASS"
else
    echo "-- pluginval VST3 (strictness $STRICTNESS): FAIL"
    exit 1
fi
