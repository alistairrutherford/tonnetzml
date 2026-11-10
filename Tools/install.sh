#!/usr/bin/env bash
# Builds TonnetzML from this checkout and installs it for the current user (PLAN.md §4.2:
# personal use, ad-hoc signed; no Developer ID or .pkg).
#
#   Tools/install.sh                  build (Release, universal), sign ad hoc, install
#   Tools/install.sh --validate       … then run pluginval (strictness 10) on the installed VST3
#   Tools/install.sh --no-build       install the last build (dist/build) again
#   Tools/install.sh --uninstall      remove the installed plug-in and app (~/Music/TonnetzML stays)
#
#   --juce DIR        the JUCE 8.0.14 checkout (default: $JUCE_DIR, else ~/Development/JUCE)
#   --no-standalone   install the VST3 only
#
# Installs, for this user only:
#   ~/Library/Audio/Plug-Ins/VST3/TonnetzML.vst3
#   ~/Applications/TonnetzML.app            (the Standalone)
#   ~/Music/TonnetzML/TestRig/             (the test rig's folder: Keys.txt, Status.txt; PLAN.md §7.3)
#
# The build happens in dist/build, a copy of TonnetzML.jucer and Source/ with the JUCE module path
# set to --juce and Projucer's plug-in copy step turned off, so the checkout (and its committed
# Xcode project, which holds the developer's JUCE path) is left alone. So it works from a clean
# clone on any account: it needs Xcode (not just the command-line tools) and a JUCE checkout, and
# builds Projucer in that checkout if it hasn't been built. It never asks for a password: nothing
# goes outside the user's home folder.
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
JUCE="${JUCE_DIR:-$HOME/Development/JUCE}"
BUILD_DIR="$REPO/dist/build"
VST3_DIR="$HOME/Library/Audio/Plug-Ins/VST3"
APP_DIR="$HOME/Applications"
RIG_DIR="$HOME/Music/TonnetzML/TestRig"
EXPECTED_JUCE="8.0.14"

build=1 validate=0 standalone=1 uninstall=0
while [[ $# -gt 0 ]]; do
    case "$1" in
        --juce) JUCE="$2"; shift ;;
        --juce=*) JUCE="${1#--juce=}" ;;
        --validate) validate=1 ;;
        --no-build) build=0 ;;
        --no-standalone) standalone=0 ;;
        --uninstall) uninstall=1 ;;
        -h|--help) awk 'NR > 1 && /^#/ { sub (/^# ?/, ""); print; next } NR > 1 { exit }' "${BASH_SOURCE[0]}"; exit 0 ;;
        *) echo "Unknown option: $1 (see --help)" >&2; exit 2 ;;
    esac
    shift
done

say () { printf '== %s\n' "$*"; }
fail () { printf 'install.sh: %s\n' "$*" >&2; exit 1; }

if [[ $uninstall == 1 ]]; then
    for b in "$VST3_DIR/TonnetzML.vst3" "$APP_DIR/TonnetzML.app"; do
        if [[ -e "$b" ]]; then rm -rf "$b"; say "removed $b"; fi
    done
    say "left ${RIG_DIR%/TestRig} alone"
    exit 0
fi

[[ "$(uname -s)" == Darwin ]] || fail "macOS only."
PRODUCTS="$BUILD_DIR/Builds/MacOSX/build/Release"

if [[ $build == 1 ]]; then
    xcodebuild -version > /dev/null 2>&1 \
        || fail "xcodebuild doesn't run: install Xcode, open it once, then: sudo xcode-select -s /Applications/Xcode.app"
    [[ -f "$JUCE/modules/juce_core/juce_core.h" ]] \
        || fail "no JUCE at $JUCE (git clone --branch $EXPECTED_JUCE https://github.com/juce-framework/JUCE.git there, or pass --juce DIR)"
    JUCE="$(cd "$JUCE" && pwd)"
    version="$(awk '{ sub (/\r$/, "") } /#define JUCE_(MAJOR|MINOR|BUILDNUMBER)/ { v = v (v ? "." : "") $3 } END { print v }' \
                   "$JUCE/modules/juce_core/system/juce_StandardHeader.h")"
    [[ "$version" == "$EXPECTED_JUCE" ]] || echo "warning: JUCE $version at $JUCE; TonnetzML is built and tested with $EXPECTED_JUCE" >&2

    PROJUCER="$JUCE/extras/Projucer/Builds/MacOSX/build/Release/Projucer.app/Contents/MacOS/Projucer"
    if [[ ! -x "$PROJUCER" ]]; then
        say "building Projucer (once, in $JUCE)"
        mkdir -p "$REPO/dist"
        # JUCE 8.0.14's Projucer project targets macOS 10.13; Xcode 27 accepts 12.0 at the lowest.
        xcodebuild -project "$JUCE/extras/Projucer/Builds/MacOSX/Projucer.xcodeproj" -configuration Release \
                   MACOSX_DEPLOYMENT_TARGET=12.0 build \
            > "$REPO/dist/projucer-build.log" 2>&1 || { tail -30 "$REPO/dist/projucer-build.log" >&2; fail "Projucer didn't build"; }
    fi

    say "preparing $BUILD_DIR (JUCE $version at $JUCE)"
    rm -rf "$BUILD_DIR"
    mkdir -p "$BUILD_DIR"
    cp "$REPO/TonnetzML.jucer" "$BUILD_DIR/"
    cp -R "$REPO/Source" "$BUILD_DIR/"
    sed -i '' -e "s#<MODULEPATH id=\"\([a-z_]*\)\" path=\"[^\"]*\"/>#<MODULEPATH id=\"\1\" path=\"$JUCE/modules\"/>#" \
              -e 's#<CONFIGURATION #<CONFIGURATION enablePluginBinaryCopyStep="0" #' "$BUILD_DIR/TonnetzML.jucer"
    "$PROJUCER" --resave "$BUILD_DIR/TonnetzML.jucer" > "$BUILD_DIR/projucer.log" 2>&1 \
        || { cat "$BUILD_DIR/projucer.log" >&2; fail "Projucer couldn't save the project"; }

    say "building VST3 and Standalone (Release, arm64 + x86_64); log: $BUILD_DIR/xcodebuild.log"
    if ! xcodebuild -project "$BUILD_DIR/Builds/MacOSX/TonnetzML.xcodeproj" -target "TonnetzML - All" -configuration Release build \
            > "$BUILD_DIR/xcodebuild.log" 2>&1; then
        grep -E "error:" "$BUILD_DIR/xcodebuild.log" | sort -u | head -20 >&2
        fail "the build failed (see $BUILD_DIR/xcodebuild.log)"
    fi
fi

[[ -d "$PRODUCTS/TonnetzML.vst3" ]] || fail "no build in $PRODUCTS: run without --no-build"

# Ad-hoc signature (no identity: "-"), then a strict check. Xcode signs ad hoc already; this
# makes the signature independent of the Xcode settings and seals the final bundle.
sign () {
    codesign --force --deep --sign - --timestamp=none "$1" > /dev/null 2>&1 || fail "codesign failed on $1"
    codesign --verify --deep --strict "$1" || fail "the signature of $1 doesn't verify"
}

install_bundle () {
    local src="$1" dest_dir="$2"
    local dest="$dest_dir/$(basename "$src")"
    mkdir -p "$dest_dir"
    rm -rf "$dest"
    ditto "$src" "$dest"
    xattr -dr com.apple.quarantine "$dest" 2> /dev/null || true
    codesign --verify --deep --strict "$dest" || fail "the installed $dest doesn't verify"
    local id archs
    id="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$dest/Contents/Info.plist")"
    archs="$(lipo -archs "$dest/Contents/MacOS/"* 2> /dev/null | head -1)"
    say "installed $dest ($id, $archs, $(codesign -dv "$dest" 2>&1 | grep -o 'Signature=.*'))"
}

sign "$PRODUCTS/TonnetzML.vst3"
install_bundle "$PRODUCTS/TonnetzML.vst3" "$VST3_DIR"
if [[ $standalone == 1 ]]; then
    sign "$PRODUCTS/TonnetzML.app"
    install_bundle "$PRODUCTS/TonnetzML.app" "$APP_DIR"
fi
mkdir -p "$RIG_DIR"

if [[ $validate == 1 ]]; then
    "$REPO/Tests/run_validation.sh"
fi

cat <<EOF
== done. In Ableton Live: Settings → Plug-Ins → turn on "Use VST3 Plug-In System Folders"
   (once), then Rescan, or restart Live. TonnetzML appears under Plug-Ins → VST3 → Alistair
   Rutherford. Put it on a MIDI track and give an instrument track "MIDI From: <that track> →
   TonnetzML" (README.md, "In Live").
EOF
