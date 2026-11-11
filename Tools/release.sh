#!/usr/bin/env bash
# Packages a TonnetzML release zip in dist/ from the Release build that Tools/install.sh makes.
#
#   Tools/release.sh            sign with Developer ID + hardened runtime, notarize, staple, zip
#   Tools/release.sh --adhoc    sign ad hoc and zip (not notarized: for this Mac, or for testers
#                               who will clear the quarantine themselves)
#   Tools/release.sh --build    run Tools/install.sh first (build Release, install), then package
#
#   --identity NAME   the signing identity (default: $DEVELOPER_ID, else the first
#                     "Developer ID Application" identity in the keychain)
#   --profile NAME    the notarytool keychain profile (default: $NOTARY_PROFILE, else "TonnetzML")
#
# Output: dist/TonnetzML-<version>-macOS.zip (or …-macOS-adhoc.zip) holding TonnetzML.vst3,
# TonnetzML.app, README.md and CHANGELOG.md, plus a .sha256 beside it. The version comes from
# TonnetzML.jucer.
#
# Notarizing needs, once per Mac (none of it is done by this script, and it never asks for or
# stores a password itself):
#   1. A "Developer ID Application" certificate in the login keychain (Apple Developer Program:
#      Xcode → Settings → Accounts → Manage Certificates → + → Developer ID Application).
#   2. A notarytool profile in the keychain, made in a terminal:
#        xcrun notarytool store-credentials TonnetzML --apple-id <id> --team-id <team>
#      (it prompts for an app-specific password from account.apple.com).
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PRODUCTS="$REPO/dist/build/Builds/MacOSX/build/Release"
STAGE="$REPO/dist/stage"

adhoc=0 build=0
IDENTITY="${DEVELOPER_ID:-}"
PROFILE="${NOTARY_PROFILE:-TonnetzML}"
while [[ $# -gt 0 ]]; do
    case "$1" in
        --adhoc) adhoc=1 ;;
        --build) build=1 ;;
        --identity) IDENTITY="$2"; shift ;;
        --identity=*) IDENTITY="${1#--identity=}" ;;
        --profile) PROFILE="$2"; shift ;;
        --profile=*) PROFILE="${1#--profile=}" ;;
        -h|--help) awk 'NR > 1 && /^#/ { sub (/^# ?/, ""); print; next } NR > 1 { exit }' "${BASH_SOURCE[0]}"; exit 0 ;;
        *) echo "Unknown option: $1 (see --help)" >&2; exit 2 ;;
    esac
    shift
done

say () { printf '== %s\n' "$*"; }
fail () { printf 'release.sh: %s\n' "$*" >&2; exit 1; }

[[ "$(uname -s)" == Darwin ]] || fail "macOS only."
VERSION="$(sed -n 's/.*jucerFormatVersion="1" version="\([^"]*\)".*/\1/p' "$REPO/TonnetzML.jucer" | head -1)"
[[ -n "$VERSION" ]] || fail "couldn't read the version from TonnetzML.jucer"

# Check the credentials before building anything, so a missing one fails fast and clearly.
if [[ $adhoc == 0 ]]; then
    if [[ -z "$IDENTITY" ]]; then
        IDENTITY="$(security find-identity -v -p codesigning \
                        | sed -n 's/.*"\(Developer ID Application: [^"]*\)".*/\1/p' | head -1)"
    fi
    missing=()
    if [[ -z "$IDENTITY" ]] || ! security find-identity -v -p codesigning | grep -qF "\"$IDENTITY\""; then
        missing+=("a \"Developer ID Application\" signing identity in the keychain${IDENTITY:+ (looked for \"$IDENTITY\")}")
    fi
    if ! xcrun notarytool history --keychain-profile "$PROFILE" > /dev/null 2>&1; then
        missing+=("a notarytool keychain profile named \"$PROFILE\" (xcrun notarytool store-credentials $PROFILE --apple-id <id> --team-id <team>)")
    fi
    if [[ ${#missing[@]} -gt 0 ]]; then
        {
            echo "release.sh: can't sign for distribution and notarize; missing:"
            for m in "${missing[@]}"; do echo "  - $m"; done
            echo "See Tools/release.sh --help. For an unnotarized, ad hoc signed zip: Tools/release.sh --adhoc"
        } >&2
        exit 1
    fi
fi

if [[ $build == 1 ]]; then
    "$REPO/Tools/install.sh"
fi
for b in TonnetzML.vst3 TonnetzML.app; do
    [[ -d "$PRODUCTS/$b" ]] || fail "no $b in $PRODUCTS: run Tools/install.sh (or this with --build) first"
done
built="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$PRODUCTS/TonnetzML.vst3/Contents/Info.plist")"
[[ "$built" == "$VERSION" ]] || fail "the build in $PRODUCTS is $built, TonnetzML.jucer says $VERSION: rebuild (--build)"

rm -rf "$STAGE"
mkdir -p "$STAGE/TonnetzML"
for b in TonnetzML.vst3 TonnetzML.app; do ditto "$PRODUCTS/$b" "$STAGE/TonnetzML/$b"; done
cp "$REPO/README.md" "$REPO/CHANGELOG.md" "$STAGE/TonnetzML/"

# Nested code first (none today, but JUCE can add helpers), then the bundle itself.
sign () {
    local bundle="$1"; shift
    while IFS= read -r -d '' nested; do
        codesign --force "$@" "$nested" > /dev/null 2>&1 || fail "codesign failed on $nested"
    done < <(find "$bundle/Contents" -mindepth 2 \( -name '*.dylib' -o -name '*.framework' -o -name '*.bundle' -o -name '*.app' -o -name '*.appex' \) -print0)
    codesign --force "$@" "$bundle" > /dev/null 2>&1 || fail "codesign failed on $bundle"
    codesign --verify --deep --strict "$bundle" || fail "the signature of $bundle doesn't verify"
}

if [[ $adhoc == 1 ]]; then
    ZIP="$REPO/dist/TonnetzML-$VERSION-macOS-adhoc.zip"
    for b in TonnetzML.vst3 TonnetzML.app; do sign "$STAGE/TonnetzML/$b" --sign - --timestamp=none; done
    say "signed ad hoc (not notarized: Gatekeeper will block it on other Macs until the quarantine is cleared)"
else
    ZIP="$REPO/dist/TonnetzML-$VERSION-macOS.zip"
    for b in TonnetzML.vst3 TonnetzML.app; do sign "$STAGE/TonnetzML/$b" --sign "$IDENTITY" --options runtime --timestamp; done
    say "signed with \"$IDENTITY\" (hardened runtime); notarizing with profile \"$PROFILE\""
    SUBMIT="$REPO/dist/notarize-$VERSION.zip"
    rm -f "$SUBMIT"
    ditto -c -k --keepParent "$STAGE/TonnetzML" "$SUBMIT"
    xcrun notarytool submit "$SUBMIT" --keychain-profile "$PROFILE" --wait > "$REPO/dist/notarize.log" 2>&1 || true
    cat "$REPO/dist/notarize.log"
    if ! grep -q 'status: Accepted' "$REPO/dist/notarize.log"; then
        id="$(sed -n 's/^ *id: //p' "$REPO/dist/notarize.log" | head -1)"
        fail "notarization wasn't accepted (dist/notarize.log; details: xcrun notarytool log ${id:-<id>} --keychain-profile $PROFILE)"
    fi
    rm -f "$SUBMIT"
    for b in TonnetzML.vst3 TonnetzML.app; do
        xcrun stapler staple "$STAGE/TonnetzML/$b" > /dev/null || fail "stapling failed on $b"
        xcrun stapler validate "$STAGE/TonnetzML/$b" > /dev/null || fail "the staple on $b doesn't validate"
    done
    spctl --assess --type execute "$STAGE/TonnetzML/TonnetzML.app" || fail "Gatekeeper rejects the app"
    say "notarized and stapled"
fi

rm -f "$ZIP" "$ZIP.sha256"
ditto -c -k --keepParent "$STAGE/TonnetzML" "$ZIP"
(cd "$(dirname "$ZIP")" && shasum -a 256 "$(basename "$ZIP")" > "$(basename "$ZIP").sha256")
rm -rf "$STAGE"
say "$ZIP ($(du -h "$ZIP" | cut -f1)); sha256 $(cut -d' ' -f1 "$ZIP.sha256")"
