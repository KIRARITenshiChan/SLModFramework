#!/usr/bin/env bash
# P8 layout B deploy: build output -> <game>\SLModFramework
# Usage: bash deploy.sh [--with-docs] [--with-sfx]
#   --with-docs  copy Workspace/docs/*.md into <framework>\docs (dev/testing only)
#   --with-sfx   copy the locally extracted Doorslam.wav into <framework>\assets\sfx
#                (game-derived asset: never ship it in a public release package)
set -euo pipefail
cd "$(dirname "$0")"

WITH_DOCS=0
WITH_SFX=0
for arg in "$@"; do
  case "$arg" in
    --with-docs) WITH_DOCS=1 ;;
    --with-sfx)  WITH_SFX=1 ;;
    *) echo "unknown arg: $arg" >&2; exit 2 ;;
  esac
done

GAME_ROOT="$(cd ../.. && pwd)"
DEST="$GAME_ROOT/SLModFramework"
[ -f "$GAME_ROOT/SisterLocation.exe" ] || { echo "game exe not found: $GAME_ROOT/SisterLocation.exe" >&2; exit 1; }
[ -f bin/launcher32.exe ] && [ -f bin/SLModFramework32.dll ] || { echo "build first: bash build.sh" >&2; exit 1; }

mkdir -p "$DEST/bin" "$DEST/config" "$DEST/plugins" "$DEST/logs/framework" "$DEST/logs/state" "$DEST/assets/sfx" "$DEST/docs"
cp -f bin/launcher32.exe bin/SLModFramework32.dll "$DEST/bin/"
cp -f ../config/framework.ini.example "$DEST/config/"

rm -rf "$DEST/plugins/HelloPlugin"
mkdir -p "$DEST/plugins/HelloPlugin"
cp -f ../plugins/HelloPlugin/plugin.ini ../plugins/HelloPlugin/main.lua "$DEST/plugins/HelloPlugin/"

if [ "$WITH_DOCS" = "1" ]; then
  cp -f ../docs/*.md "$DEST/docs/" 2>/dev/null || true
fi
if [ "$WITH_SFX" = "1" ] && [ -f ../assets/sfx/Doorslam.wav ]; then
  cp -f ../assets/sfx/Doorslam.wav "$DEST/assets/sfx/"
fi

echo "deployed to: $DEST"
ls -l "$DEST/bin"
sha256sum "$DEST/bin/launcher32.exe" "$DEST/bin/SLModFramework32.dll"
