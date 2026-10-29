#!/bin/bash
# Usage: scripts/package_mac.sh
#   Release universal build (arm64 + x86_64, build-release) -> dist/DontGoCat.app
#   (macdeployqt + unused Qt plugins removed + license files + ad-hoc signature)
#   -> dist/DontGoCat-<version>.dmg (drag DontGoCat.app onto the Applications link).
#   Single source of the version: project(DontGoCat VERSION ...) in CMakeLists.txt (build-release/ccat_version.txt).
#   macdeployqt is read from the CMake cache (MACDEPLOYQT_EXECUTABLE).
#
#   Not notarized (no Apple Developer ID): Gatekeeper blocks the first launch of a downloaded copy.
#   Users allow it once in System Settings > Privacy & Security > "Open Anyway",
#   or run: xattr -dr com.apple.quarantine /Applications/DontGoCat.app
#   Move the app to /Applications before the first launch: an app started from the DMG / Downloads is
#   translocated to a temporary path, and launch-at-login would then point at that path.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$ROOT/build-release"
DIST="$ROOT/dist"
APP="$DIST/DontGoCat.app"

fail() {
    echo "Packaging failed: $*" >&2
    exit 1
}

echo "[1/5] Release build (build-release, arm64 + x86_64)"
cmake -S "$ROOT" -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCCAT_BUILD_TESTS=OFF \
    "-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64"
cmake --build "$BUILD_DIR" --target ccat

VERSION="$(cat "$BUILD_DIR/ccat_version.txt" 2>/dev/null || true)"
[ -n "$VERSION" ] || fail "could not read the version from $BUILD_DIR/ccat_version.txt"
echo "    version $VERSION"

MACDEPLOYQT="$(sed -n 's/^MACDEPLOYQT_EXECUTABLE:FILEPATH=//p' "$BUILD_DIR/CMakeCache.txt")"
[ -x "$MACDEPLOYQT" ] || fail "could not find macdeployqt in the CMake cache"

echo "[2/5] Assemble dist/DontGoCat.app (macdeployqt)"
pgrep -qf "$APP/Contents/MacOS/DontGoCat" && fail "quit the DontGoCat running from dist/ first"
mkdir -p "$DIST"
rm -rf "$APP"
ditto "$BUILD_DIR/DontGoCat.app" "$APP"
"$MACDEPLOYQT" "$APP"

# The app uses Core/Gui/Widgets/Network + platforms/qcocoa + styles/qmacstyle + tls.
# Network is only the update check (HTTPS to api.github.com) -> Secure Transport TLS backend only;
# the OpenSSL backend (no OpenSSL libraries shipped) and the cert-only backend are removed.
# Images come from the built-in PNG handler (Qt resources), so imageformats / iconengines (svg, and QtSvg) /
# networkinformation are not needed.
PLUGINS="$APP/Contents/PlugIns"
rm -f "$PLUGINS/tls/libqopensslbackend.dylib" "$PLUGINS/tls/libqcertonlybackend.dylib"
rm -rf "$PLUGINS/imageformats" "$PLUGINS/iconengines" "$PLUGINS/networkinformation"
rm -rf "$APP/Contents/Frameworks/QtSvg.framework"

# License files (app license, third-party notices, Qt LGPL/GPL texts) inside the bundle
RES="$APP/Contents/Resources"
cp "$ROOT/LICENSE" "$RES/LICENSE.txt"
cp "$ROOT/THIRD_PARTY_NOTICES.txt" "$RES/THIRD_PARTY_NOTICES.txt"
mkdir -p "$RES/licenses"
cp "$ROOT/licenses/Qt-LGPL-3.0-GPL-3.0.txt" "$RES/licenses/Qt-LGPL-3.0-GPL-3.0.txt"

echo "[3/5] Ad-hoc signature"
# The bundle changed after linking / macdeployqt -> re-sign everything (Apple Silicon refuses unsigned code)
codesign --force --deep --sign - "$APP"
codesign --verify --deep --strict "$APP"

echo "[4/5] DMG"
DMG="$DIST/DontGoCat-$VERSION.dmg"
STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT
ditto "$APP" "$STAGE/DontGoCat.app"
ln -s /Applications "$STAGE/Applications"
rm -f "$DMG"
hdiutil create -volname "DontGoCat" -srcfolder "$STAGE" -format UDZO -ov "$DMG" >/dev/null
hdiutil verify "$DMG" >/dev/null

echo "[5/5] Done"
echo "    $DMG"
echo "    Not notarized: first launch needs System Settings > Privacy & Security > \"Open Anyway\"."
