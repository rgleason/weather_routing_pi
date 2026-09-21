#!/usr/bin/env bash
set -euo pipefail

OUT=/tmp/macos_deps_universal.tar.xz

# Minimal set; extend as needed.
INCLUDE_PATHS=(
  "/usr/local/include/wx-3.2"
  "/usr/local/lib/wx/include/osx_cocoa-unicode-3.2"
)

LIB_PATHS=(
  "/usr/local/lib/libwx_osx_cocoa-3.2.dylib"
  "/usr/local/lib/libwx_osx_cocoa-3.2.a"
  "/usr/local/lib/libwx_baseu-3.2.dylib"
  "/usr/local/lib/libwx_baseu-3.2.a"
)

BIN_PATHS=(
  "/usr/local/bin/wx-config"
)

TMPDIR=$(mktemp -d)
trap 'rm -rf "${TMPDIR}"' EXIT

mkdir -p "${TMPDIR}/usr/local"

for p in "${INCLUDE_PATHS[@]}"; do
  rsync -a "${p}" "${TMPDIR}${p}"
done

for p in "${LIB_PATHS[@]}"; do
  rsync -a "${p}" "${TMPDIR}${p}"
done

for p in "${BIN_PATHS[@]}"; do
  rsync -a "${p}" "${TMPDIR}${p}"
done

cd "${TMPDIR}"
tar -cJf "${OUT}" usr/local

echo "Created ${OUT}"
