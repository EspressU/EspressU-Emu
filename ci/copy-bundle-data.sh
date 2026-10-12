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
mkdir -p "$DATA_ROOT/resources"
rsync -a --delete "$SRC_FONTS/" "$DATA_ROOT/resources/sharedFonts/"
rsync -a --delete "$SRC_PROFILES/" "$DATA_ROOT/gameProfiles/"

rm -rf "$DEST/resources" "$DEST/gameProfiles"

echo "copy-bundle-data.sh: $(find "$DATA_ROOT/resources/sharedFonts" -type f | wc -l | tr -d ' ') shared font(s), $(find "$DATA_ROOT/gameProfiles" -type f | wc -l | tr -d ' ') game profile(s) -> $DATA_ROOT"
