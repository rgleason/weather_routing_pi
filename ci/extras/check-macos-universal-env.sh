#!/usr/bin/env bash
set -euo pipefail

echo "== SDK =="
xcrun --sdk macosx --show-sdk-path || echo "SDK missing"

echo "== wx-config =="
if ! command -v wx-config >/dev/null 2>&1; then
  echo "wx-config not found"
  exit 1
fi

wx-config --version
wx-config --cxxflags
wx-config --libs std,aui,gl

echo "== wx headers =="
ls -d /usr/local/include/wx-3.2 || echo "wx-3.2 headers missing"
ls -d /usr/local/lib/wx/include/osx_cocoa-unicode-3.2 || echo "wx setup.h dir missing"

echo "== wx libs =="
ls /usr/local/lib/libwx_osx_cocoa-3.2.* || echo "wx cocoa libs missing"
ls /usr/local/lib/libwx_baseu-3.2.* || echo "wx base libs missing"

echo "== CPack config =="
ls cmake/PluginCPackOptions.cmake || echo "PluginCPackOptions.cmake missing"

echo "== CMake generator sanity =="
cmake --version
