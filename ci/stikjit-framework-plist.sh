#!/bin/bash
# Writes the Info.plist StikJIT's build leaves out into a StikJIT.framework (see ci/build-stikjit.sh).
# The minimum iOS version is read from the binary itself.
#
# Usage: ci/stikjit-framework-plist.sh path/to/StikJIT.framework
set -eo pipefail

FRAMEWORK="$1"
MINOS="$(otool -l "$FRAMEWORK/StikJIT" | awk '/LC_BUILD_VERSION/{f=1} f&&/minos/{print $2; exit}')"

cat > "$FRAMEWORK/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>CFBundleDevelopmentRegion</key>
	<string>en</string>
	<key>CFBundleExecutable</key>
	<string>StikJIT</string>
	<key>CFBundleIdentifier</key>
	<string>com.stik.StikJIT</string>
	<key>CFBundleInfoDictionaryVersion</key>
	<string>6.0</string>
	<key>CFBundleName</key>
	<string>StikJIT</string>
	<key>CFBundlePackageType</key>
	<string>FMWK</string>
	<key>CFBundleShortVersionString</key>
	<string>1.0</string>
	<key>CFBundleVersion</key>
	<string>1</string>
	<key>CFBundleSupportedPlatforms</key>
	<array>
		<string>iPhoneOS</string>
	</array>
	<key>MinimumOSVersion</key>
	<string>${MINOS:-17.4}</string>
</dict>
</plist>
EOF
plutil -lint "$FRAMEWORK/Info.plist"
