#!/bin/bash
# Builds StikJIT.xcframework (https://github.com/StikDebug/StikJIT, MPL-2.0) from source into
set -eo pipefail

STIKJIT_COMMIT=32287268fa5824f9edce4cb359f5833ce0cf7b00
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/dependencies/StikJIT.xcframework"
IDEVICE_OUT="$ROOT/dependencies/idevice"
STAMP="$ROOT/dependencies/.stikjit-commit"

if [ -d "$OUT" ] && [ -f "$IDEVICE_OUT/libidevice_ffi.a" ] && [ -f "$STAMP" ] && [ "$(cat "$STAMP")" = "$STIKJIT_COMMIT" ]; then
  echo "StikJIT.xcframework is already at $STIKJIT_COMMIT"
  exit 0
fi

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

git -C "$WORK" init -q stikjit
git -C "$WORK/stikjit" remote add origin https://github.com/StikDebug/StikJIT.git
git -C "$WORK/stikjit" fetch -q --depth 1 origin "$STIKJIT_COMMIT"
git -C "$WORK/stikjit" checkout -q FETCH_HEAD

cd "$WORK/stikjit"
xcodegen generate
xcodebuild archive \
  -project StikJIT.xcodeproj \
  -scheme StikJIT \
  -destination 'generic/platform=iOS' \
  -archivePath "$WORK/StikJIT-iOS" \
  SKIP_INSTALL=NO \
  BUILD_LIBRARY_FOR_DISTRIBUTION=YES \
  CODE_SIGNING_ALLOWED=NO

FRAMEWORK="$WORK/StikJIT-iOS.xcarchive/Products/Library/Frameworks/StikJIT.framework"
if [ ! -f "$FRAMEWORK/Info.plist" ]; then
  "$ROOT/ci/stikjit-framework-plist.sh" "$FRAMEWORK"
fi

rm -rf "$IDEVICE_OUT"
mkdir -p "$IDEVICE_OUT"
cp "$WORK/stikjit/idevice/libidevice_ffi.a" "$WORK/stikjit/idevice/idevice.h" "$IDEVICE_OUT/"

rm -rf "$OUT"
xcodebuild -create-xcframework \
  -framework "$FRAMEWORK" \
  -output "$OUT"
echo "$STIKJIT_COMMIT" > "$STAMP"
echo "Built $OUT at $STIKJIT_COMMIT"
