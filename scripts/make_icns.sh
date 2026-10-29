#!/bin/sh
# Usage: scripts/make_icns.sh <assets dir> <out.icns>
#   assets/tray_face_{16,32,64,512,1024}.png -> macOS app icon (.icns). Called by CMake (APPLE) at build time.
#   128 / 256 are halved from 512 by sips: 512 is an exact 8x upscale of the 64px pixel art, so 2x / 4x
#   downscales stay on block boundaries and remain sharp.
set -eu

ASSETS="$1"
OUT="$2"
SET="${OUT%.icns}.iconset"

rm -rf "$SET"
mkdir -p "$SET"
cp "$ASSETS/tray_face_16.png"   "$SET/icon_16x16.png"
cp "$ASSETS/tray_face_32.png"   "$SET/icon_16x16@2x.png"
cp "$ASSETS/tray_face_32.png"   "$SET/icon_32x32.png"
cp "$ASSETS/tray_face_64.png"   "$SET/icon_32x32@2x.png"
sips -z 128 128 "$ASSETS/tray_face_512.png" --out "$SET/icon_128x128.png" >/dev/null
sips -z 256 256 "$ASSETS/tray_face_512.png" --out "$SET/icon_128x128@2x.png" >/dev/null
cp "$SET/icon_128x128@2x.png"   "$SET/icon_256x256.png"
cp "$ASSETS/tray_face_512.png"  "$SET/icon_256x256@2x.png"
cp "$ASSETS/tray_face_512.png"  "$SET/icon_512x512.png"
cp "$ASSETS/tray_face_1024.png" "$SET/icon_512x512@2x.png"
iconutil -c icns "$SET" -o "$OUT"
rm -rf "$SET"
