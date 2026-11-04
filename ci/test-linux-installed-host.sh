#!/usr/bin/env bash
# Build an unmodified, pinned stock host in the package's actual distro/ABI.
set -euo pipefail

source_dir=${1:?plugin source directory is required}
work_dir=${2:?disposable build directory is required}
archive=${3:?final importable plugin archive is required}
output=${4:?runtime evidence directory is required}

host_work="$work_dir/stock-host"
mkdir -p "$host_work"
# Cache compilation only. Configure, link, install and every runtime check
# still run against a freshly unpacked, hash-verified stock host source tree.
compiler_cache=()
if [[ -n "${CCACHE_DIR:-}" ]]; then
  command -v ccache >/dev/null
  export CCACHE_CONFIGPATH=/dev/null CCACHE_COMPILERCHECK=content CCACHE_MAXSIZE=750M
  export CCACHE_SLOPPINESS=""
  compiler_cache=(-DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache)
  ccache --zero-stats
fi
tarball=$(python3 "$source_dir/ci/fetch-stock-host.py" linux-source "$host_work/cache")
host_source="$host_work/source"
if [[ ! -f "$host_source/CMakeLists.txt" ]]; then
  mkdir -p "$host_source"
  tar -xzf "$tarball" --strip-components=1 -C "$host_source"
fi
cmake -S "$host_source" -B "$host_work/build" -G Ninja \
  "${compiler_cache[@]}" \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local \
  -DOCPN_BUILD_TEST=OFF -DOCPN_USE_BUNDLED_LIBS=ON \
  -DOCPN_USE_WEBVIEW=OFF -DOCPN_BUNDLE_DOCS=OFF \
  -DOCPN_BUNDLE_GSHHS=ON -DOCPN_BUNDLE_TCDATA=ON
cmake --build "$host_work/build" --parallel "${CMAKE_BUILD_PARALLEL_LEVEL:-3}"
if [[ -n "${CCACHE_DIR:-}" ]]; then
  cache_log_dir="$(dirname "$output")/logs"
  mkdir -p "$cache_log_dir"
  ccache --show-stats | tee "$cache_log_dir/stock-host-compiler-cache.log"
fi
cmake --install "$host_work/build"
dbus-run-session -- python3 "$source_dir/ci/test-installed-host.py" \
  /usr/local/bin/opencpn /usr/local/bin/opencpn-cmd "$archive" "$output"
