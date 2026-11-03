#!/bin/bash
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC_FONTS="$REPO_ROOT/bin/resources/sharedFonts"
SRC_PROFILES="$REPO_ROOT/bin/gameProfiles"

DEST="${BUILT_PRODUCTS_DIR:?not running inside an Xcode build}/${UNLOCALIZED_RESOURCES_FOLDER_PATH:?}"

for d in "$SRC_FONTS" "$SRC_PROFILES"; do
    if [ ! -d "$d" ]; then
        echo "error: copy-bundle-data.sh: required data directory not found: $d" >&2
        exit 1
    fi
done

DATA_ROOT="$DEST/CemuData"
STAGING_ROOT="$(mktemp -d "${TEMP_DIR:?}/cemu-bundle-data.XXXXXX")"
OLD_DATA_ROOT="$STAGING_ROOT/previous"
trap 'rm -rf "$STAGING_ROOT"' EXIT

mkdir -p "$STAGING_ROOT/new/resources"
cp -R "$SRC_FONTS" "$STAGING_ROOT/new/resources/sharedFonts"
cp -R "$SRC_PROFILES" "$STAGING_ROOT/new/gameProfiles"

if [ -e "$DATA_ROOT" ]; then
    mv "$DATA_ROOT" "$OLD_DATA_ROOT"
fi
mv "$STAGING_ROOT/new" "$DATA_ROOT"

rm -rf "$DEST/resources" "$DEST/gameProfiles"

echo "copy-bundle-data.sh: copied shared fonts and game profiles -> $DATA_ROOT"
