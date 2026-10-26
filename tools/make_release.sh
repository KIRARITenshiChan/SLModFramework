#!/usr/bin/env bash
# P8.1/P8.4 release packager.
# Usage: make_release.sh <version> [--allow-missing-bin]
# Builds Workspace/release/SLModFramework-<version>-win-x86/ from an explicit
# whitelist and writes BUILD_MANIFEST.sha256 + ZIP + SHA256SUMS.
# Hard rules: never package game-derived assets, dumps, logs, recon, analysis.
set -euo pipefail

WS="$(cd "$(dirname "$0")/.." && pwd)"
VER="${1:-}"
if [ -z "$VER" ]; then
  echo "usage: $0 <version>   e.g. $0 1.0.0" >&2
  exit 2
fi

OUT="${WS}/release"
PKG="SLModFramework-${VER}-win-x86"
STAGE="${OUT}/${PKG}"
BIN="${WS}/framework/bin"

echo "[release] version=${VER}"
echo "[release] workspace=${WS}"

for f in "${BIN}/launcher32.exe" "${BIN}/SLModFramework32.dll"; do
  if [ ! -f "$f" ]; then echo "[release] ERROR missing $f (run framework/build.sh)" >&2; exit 1; fi
done

# version string sanity inside DLL (ASCII search; build defines SLMODF_VERSION)
if ! grep -qa "${VER}" "${BIN}/SLModFramework32.dll"; then
  echo "[release] WARNING: DLL does not contain version string '${VER}' (did you rebuild?)" >&2
fi

rm -rf "${STAGE}"
mkdir -p "${STAGE}/bin" "${STAGE}/config" "${STAGE}/plugins/HelloPlugin" "${STAGE}/docs/third_party"

copy() { # src dst
  if [ ! -e "$1" ]; then echo "[release] ERROR missing $1" >&2; exit 1; fi
  cp -a "$1" "$2"
}

# ---- binaries ----
copy "${BIN}/launcher32.exe" "${STAGE}/bin/launcher32.exe"
copy "${BIN}/SLModFramework32.dll" "${STAGE}/bin/SLModFramework32.dll"

# ---- config template (never ship a live framework.ini) ----
copy "${WS}/config/framework.ini.example" "${STAGE}/config/framework.ini.example"

# ---- bundled sample plugin (HelloPlugin only, per m12195) ----
copy "${WS}/plugins/HelloPlugin/plugin.ini" "${STAGE}/plugins/HelloPlugin/plugin.ini"
copy "${WS}/plugins/HelloPlugin/main.lua" "${STAGE}/plugins/HelloPlugin/main.lua"

# ---- user docs (whitelist) ----
for d in README.md INSTALL.md UNINSTALL.md TROUBLESHOOTING.md CHANGELOG.md PLUGIN_API.md; do
  copy "${WS}/docs/release/${d}" "${STAGE}/docs/${d}"
done
copy "${WS}/THIRD_PARTY_NOTICES.md" "${STAGE}/docs/THIRD_PARTY_NOTICES.md"
copy "${WS}/THIRD_PARTY_NOTICES.md" "${STAGE}/THIRD_PARTY_NOTICES.md"

# ---- licenses ----
copy "${WS}/LICENSE" "${STAGE}/LICENSE"
copy "${WS}/third_party/imgui/LICENSE.txt" "${STAGE}/docs/third_party/imgui-LICENSE.txt"
copy "${WS}/third_party/minhook/LICENSE.txt" "${STAGE}/docs/third_party/minhook-LICENSE.txt"
# Lua MIT text (upstream COPYRIGHT not vendored): extract from our notices file
awk '/^Copyright \(C\) 1994-2024 Lua.org/{flag=1} flag{print} /^THE SOFTWARE IS PROVIDED/{if(flag) exit}' \
  "${WS}/THIRD_PARTY_NOTICES.md" > "${STAGE}/docs/third_party/lua-LICENSE.txt" || true
if [ ! -s "${STAGE}/docs/third_party/lua-LICENSE.txt" ]; then
  echo "Copyright (C) 1994-2024 Lua.org, PUC-Rio." > "${STAGE}/docs/third_party/lua-LICENSE.txt"
  sed -n '/^Permission is hereby granted/,/^OUT OF OR IN CONNECTION/d' "${WS}/THIRD_PARTY_NOTICES.md" >> "${STAGE}/docs/third_party/lua-LICENSE.txt"
fi

# ---- release root README ----
copy "${WS}/docs/release/README.md" "${STAGE}/README.md"

# ---- forbid accidental game-derived content ----
forbidden=$(find "${STAGE}" -type f \( -iname '*.wav' -o -iname '*.mp3' -o -iname '*.ogg' -o -iname '*.bin' -o -iname '*.dump' \) -print || true)
if [ -n "${forbidden}" ]; then echo "[release] ERROR forbidden media in stage:" >&2; echo "${forbidden}" >&2; exit 1; fi

# ---- manifest (relative paths, sorted) ----
MANIFEST="${OUT}/BUILD_MANIFEST.sha256"
( cd "${STAGE}" && find . -type f -print | sed 's|^\./||' | LC_ALL=C sort | while IFS= read -r f; do
    printf '%s  %s\n' "$(sha256sum "$f" | cut -d' ' -f1)" "$f"
  done ) > "${MANIFEST}"

# ---- deterministic zip (fixed mtime, stored dirs) ----
OUT_WIN="$(cygpath -w "${OUT}" 2>/dev/null || echo "${OUT}")"
python - "${OUT_WIN}" "${PKG}" <<'PY'
import os, sys, zipfile
out, pkg = sys.argv[1], sys.argv[2]
src = os.path.join(out, pkg)
zpath = os.path.join(out, pkg + ".zip")
with zipfile.ZipFile(zpath, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
    for root, dirs, files in os.walk(src):
        dirs.sort(); files.sort()
        for name in files:
            full = os.path.join(root, name)
            rel = os.path.relpath(full, src).replace(os.sep, "/")
            zi = zipfile.ZipInfo("SLModFramework/" + rel, date_time=(2026, 10, 27, 0, 0, 0))
            zi.compress_type = zipfile.ZIP_DEFLATED
            zi.external_attr = 0o644 << 16
            with open(full, "rb") as fh:
                z.writestr(zi, fh.read())
print("zip:", zpath, os.path.getsize(zpath), "bytes")
PY

ZIP="${OUT}/${PKG}.zip"
SUMS="${OUT}/SHA256SUMS"
{
  printf '%s  %s\n' "$(sha256sum "${STAGE}/bin/launcher32.exe" | cut -d' ' -f1)" "bin/launcher32.exe"
  printf '%s  %s\n' "$(sha256sum "${STAGE}/bin/SLModFramework32.dll" | cut -d' ' -f1)" "bin/SLModFramework32.dll"
  printf '%s  %s\n' "$(sha256sum "${ZIP}" | cut -d' ' -f1)" "${PKG}.zip"
} > "${SUMS}"

echo "[release] staged: ${STAGE}"
echo "[release] manifest: ${MANIFEST} ($(wc -l < "${MANIFEST}") files)"
echo "[release] zip: ${ZIP} ($(stat -c%s "${ZIP}") bytes)"
cat "${SUMS}"
