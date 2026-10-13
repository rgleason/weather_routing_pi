#!/usr/bin/env bash
# Build an unmodified, pinned stock host in the package's actual distro/ABI.
set -euo pipefail

source_dir=${1:?plugin source directory is required}
work_dir=${2:?disposable build directory is required}
archive=${3:?final importable plugin archive is required}
output=${4:?runtime evidence directory is required}

host_work="$work_dir/stock-host"
mkdir -p "$host_work"
tarball=$(python3 "$source_dir/ci/fetch-stock-host.py" linux-source "$host_work/cache")
host_source="$host_work/source"
if [[ ! -f "$host_source/CMakeLists.txt" ]]; then
  mkdir -p "$host_source"
  tar -xzf "$tarball" --strip-components=1 -C "$host_source"
fi
cmake -S "$host_source" -B "$host_work/build" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local \
  -DOCPN_BUILD_TEST=OFF -DOCPN_USE_BUNDLED_LIBS=ON \
  -DOCPN_USE_WEBVIEW=OFF -DOCPN_BUNDLE_DOCS=OFF \
  -DOCPN_BUNDLE_GSHHS=ON -DOCPN_BUNDLE_TCDATA=ON
cmake --build "$host_work/build" --parallel "${CMAKE_BUILD_PARALLEL_LEVEL:-3}"
cmake --install "$host_work/build"
dbus-run-session -- python3 "$source_dir/ci/test-installed-host.py" \
  /usr/local/bin/opencpn /usr/local/bin/opencpn-cmd "$archive" "$output"
