#!/bin/bash
# Builds an unsigned EspressU IPA without any local paths and scans it for leaks.
# Used by the GitHub Actions workflows; also works locally on macOS.
#
# Usage: ci/build-ipa.sh <output.ipa>
# Env:   ESPRESSU_BUILD_DIR   clean build location (default /private/tmp/espressu-build)
#        VCPKG_BINARY_SOURCES vcpkg binary cache (default: disabled)
#        ESPRESSU_CCACHE=1    use ccache for the C/C++ core build
set -euo pipefail

SRC="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$(cd "$(dirname "${1:?usage: ci/build-ipa.sh <output.ipa>}")" && pwd)/$(basename "$1")"
WORK="${ESPRESSU_BUILD_DIR:-/private/tmp/espressu-build}"
TREE="$WORK/src-tree"
export VCPKG_BINARY_SOURCES="${VCPKG_BINARY_SOURCES:-clear}"
MAP="-ffile-prefix-map=$TREE=. -fdebug-prefix-map=$TREE=."

step() { echo; echo "=== $* ==="; }

step "Copy sources to a clean tree"
mkdir -p "$TREE"
rsync -a --delete --exclude .git --exclude build-ios --exclude '*.xcodeproj' --exclude .DS_Store \
  --exclude build-espressu-ipa.sh --exclude .github \
  --exclude dependencies/StikJIT.xcframework --exclude dependencies/idevice --exclude dependencies/.stikjit-commit \
  --exclude dependencies/vcpkg/buildtrees --exclude dependencies/vcpkg/downloads --exclude dependencies/vcpkg/packages \
  "$SRC/" "$TREE/"

step "Prepare vcpkg"
VT="$TREE/dependencies/vcpkg"
if ! git --git-dir="$VT/.git" rev-parse --verify -q HEAD >/dev/null 2>&1; then
  VG="$(git -C "$SRC/dependencies/vcpkg" rev-parse --absolute-git-dir)"
  rm -rf "$VT/.git"
  cp -R "$VG" "$VT/.git"
  git config --file "$VT/.git/config" --unset core.worktree || true
fi
rm -rf "$VT/buildtrees/versioning_"

step "Build StikJIT"
# Newer Swift compilers cannot compile StikJIT's textual module interface (enum StikJIT shadows module StikJIT),
# so build it without library evolution and use the binary module instead.
( cd "$TREE" && STIKJIT_LIBRARY_EVOLUTION=NO ci/build-stikjit.sh )

step "Configure (CMake + vcpkg dependencies)"
CCACHE_ARGS=()
if [ "${ESPRESSU_CCACHE:-0}" = 1 ]; then
  CCACHE_ARGS=(-DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache -DCMAKE_OBJCXX_COMPILER_LAUNCHER=ccache)
  export CCACHE_BASEDIR="$TREE"
fi
cd "$TREE"
cmake -S . -B build-ios -G Ninja \
  -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_SYSROOT=iphoneos -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=17.4 -DVCPKG_TARGET_TRIPLET=arm64-ios \
  -DBUILD_HEADLESS_DYLIB=ON -DCMAKE_MACOSX_BUNDLE=OFF -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_FLAGS="$MAP" -DCMAKE_CXX_FLAGS="$MAP" -DCMAKE_OBJCXX_FLAGS="$MAP" \
  ${CCACHE_ARGS[@]+"${CCACHE_ARGS[@]}"}

step "Build emulator core"
cmake --build build-ios --target CemuBin
mkdir -p build-ios/out
rm -rf build-ios/out/Cemu.framework
cp -R build-ios/src/Cemu.framework build-ios/out/

step "Build app"
cd "$TREE/src/ios"
rm -rf "$WORK/dd/Build/Products"
xcodegen generate
xcodebuild -project EspressU.xcodeproj -scheme EspressU -sdk iphoneos -configuration Release \
  -derivedDataPath "$WORK/dd" CODE_SIGNING_ALLOWED=NO DEBUG_INFORMATION_FORMAT=none \
  "OTHER_SWIFT_FLAGS=\$(inherited) -Xfrontend -no-serialize-debugging-options -debug-prefix-map $TREE=." build

step "Package IPA"
PKG="$WORK/pkg"
rm -rf "$PKG"
mkdir -p "$PKG/Payload"
cp -R "$WORK/dd/Build/Products/Release-iphoneos/EspressU.app" "$PKG/Payload/"
find "$PKG/Payload" -name '*.dSYM' -prune -exec rm -rf {} +
find "$PKG/Payload" -type f \( -perm -u+x -o -name '*.dylib' \) -exec file {} \; \
  | grep Mach-O | cut -d: -f1 | while read -r binary; do strip -x "$binary" 2>/dev/null || true; done
rm -f "$OUT"
( cd "$PKG" && zip -qry "$OUT" Payload )

step "Scan IPA for local paths"
# Anything that identifies a build machine or developer must not be in the IPA.
PATTERN='amardzambic|expressu-ios|/Users/|/home/runner|runner/work|/private/tmp|espressu-build|buildtrees|/opt/homebrew|/Applications/Xcode'
SCAN="$WORK/scan"
rm -rf "$SCAN"; mkdir -p "$SCAN"
unzip -q "$OUT" -d "$SCAN"
if grep -raEo "[[:print:]]{0,40}($PATTERN)[[:print:]]{0,40}" "$SCAN" | head -50 | grep .; then
  echo "ERROR: local paths found in the IPA (see above). Refusing to publish."
  rm -f "$OUT"
  exit 1
fi
echo "Scan clean."
ls -lh "$OUT"
