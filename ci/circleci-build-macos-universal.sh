#!/usr/bin/env bash
# Native Apple-Silicon validation build.  The historical filename is retained
# because it is part of the Frontend2 CI interface.
set -euo pipefail
set -x

repo=$(cd "$(dirname "$0")/.." && pwd)
cd "$repo"
git submodule update --init --recursive

export HOMEBREW_NO_AUTO_UPDATE=1
while IFS= read -r package; do
  case "$package" in
    ''|'#'*) continue ;;
  esac
  # Homebrew may read stdin while installing a formula.  Keep it from
  # consuming the remaining entries in macos-deps.
  brew list --versions "$package" >/dev/null 2>&1 ||
    brew install "$package" </dev/null
done <build-deps/macos-deps

brew_prefix=$(brew --prefix)
wx_prefix=$(brew --prefix wxwidgets@3.2)
export PATH="${wx_prefix}/bin:${brew_prefix}/opt/gettext/bin:${brew_prefix}/bin:${PATH}"
export PKG_CONFIG_PATH="${wx_prefix}/lib/pkgconfig:${brew_prefix}/lib/pkgconfig:${brew_prefix}/opt/openssl@3/lib/pkgconfig"
export CMAKE_PREFIX_PATH="${wx_prefix};${brew_prefix}"
export WX_CONFIG="${wx_prefix}/bin/wx-config-3.2"
export OCPN_TARGET=macos-arm64
export WX_VER=32

# Some Apple-Silicon Homebrew images have shipped a gettext bottle whose
# msgfmt crashes with SIGSEGV.  Exercise the largest catalogue before the
# parallel build and rebuild only a broken bottle from the official formula,
# matching the guard proven by xGRIB's native macOS job.
msgfmt_smoke="${TMPDIR:-/tmp}/weather-routing-msgfmt-smoke.mo"
if ! msgfmt --check -o "$msgfmt_smoke" po/el_GR.po; then
  brew reinstall --build-from-source gettext </dev/null
  msgfmt --check -o "$msgfmt_smoke" po/el_GR.po
fi
rm -f "$msgfmt_smoke"

build_tests="$repo/build-tests"
build_package="$repo/build-package"
stage="$repo/stage"
artifact="$repo/artifacts/macos-arm64"
log_dir="$artifact/logs"
test_dir="$artifact/tests"
package_dir="$artifact/package"
mkdir -p "$build_tests" "$build_package" "$stage" \
  "$log_dir" "$test_dir" "$package_dir"

while IFS= read -r package; do
  case "$package" in
    ''|'#'*) continue ;;
  esac
  brew list --versions "$package"
done <build-deps/macos-deps >"$log_dir/dependencies.log"

common_args=(
  -DCMAKE_BUILD_TYPE=Release
  -DCMAKE_OSX_ARCHITECTURES=arm64
  -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0
  "-DCMAKE_PREFIX_PATH=$CMAKE_PREFIX_PATH"
  "-DwxWidgets_CONFIG_EXECUTABLE=$WX_CONFIG"
  -DWEATHER_ROUTING_STANDALONE_API=ON
)

cmake -S . -B "$build_tests" "${common_args[@]}" \
  -DOCPN_BUILD_TEST=ON 2>&1 | tee "$log_dir/configure-tests.log"
cmake --build "$build_tests" --parallel 3 \
  2>&1 | tee "$log_dir/build-tests.log"
ctest --test-dir "$build_tests" --output-on-failure \
  --output-junit "$test_dir/ctest.xml" \
  2>&1 | tee "$log_dir/test.log"

# Keep GoogleTest and its discovery products out of the catalogue archive.
cmake -S . -B "$build_package" "${common_args[@]}" \
  -DOCPN_BUILD_TEST=OFF 2>&1 | tee "$log_dir/configure-package.log"
cmake --build "$build_package" --parallel 3 \
  2>&1 | tee "$log_dir/build-package.log"
cmake --install "$build_package" --prefix "$stage" \
  2>&1 | tee "$log_dir/install.log"
cmake --build "$build_package" --target package \
  2>&1 | tee "$log_dir/package.log"

shopt -s nullglob
archives=("$build_package"/weather_routing_pi-*.tar.gz)
metadata_files=("$build_package"/weather_routing_pi-*.xml)
test "${#archives[@]}" -eq 1
test "${#metadata_files[@]}" -eq 1
archive_source=${archives[0]}
metadata_source=${metadata_files[0]}

cp -f "$archive_source" "$metadata_source" "$package_dir/"
(cd "$package_dir" && find . -maxdepth 1 -type f ! -name SHA256SUMS \
  -print0 | xargs -0 shasum -a 256 >SHA256SUMS)
archive="$package_dir/$(basename "$archive_source")"
metadata="$package_dir/$(basename "$metadata_source")"
tar -tzf "$archive" >"$test_dir/archive-contents.txt"
grep -q 'OpenCPN.app/Contents/PlugIns/libweather_routing_pi.dylib$' \
  "$test_dir/archive-contents.txt"
grep -q 'OpenCPN.app/Contents/SharedSupport/plugins/weather_routing_pi/data/' \
  "$test_dir/archive-contents.txt"
if grep -Eqi 'libg(test|mock)|libxweather_routing_pi\.dylib|opencpn-xweather_routing_pi\.mo' \
    "$test_dir/archive-contents.txt"; then
  echo "Archive contains a test library or preview plugin identity" >&2
  exit 1
fi
grep -q '<name> WeatherRouting </name>' "$metadata"
grep -q '<api-version> 1.21 </api-version>' "$metadata"
grep -q '<target>darwin-wx32</target>' "$metadata"
grep -q '<source> https://github.com/pob220/weather_routing_pi </source>' \
  "$metadata"

package_version=$(sed -n \
  's:.*<version>[[:space:]]*\([^[:space:]<]*\)[[:space:]]*</version>.*:\1:p' \
  "$metadata")
if [[ ! "$package_version" =~ ^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
  echo "Invalid or missing package version in $metadata" >&2
  exit 1
fi

jq -n \
  --arg commit "$(git rev-parse HEAD)" \
  --arg os "$(sw_vers -productVersion)" \
  --arg compiler "$(c++ --version | head -1)" \
  --arg cmake "$(cmake --version | head -1)" \
  --arg wx "$("$WX_CONFIG" --version)" \
  --arg version "$package_version" \
  --arg package "$(basename "$archive")" \
  --arg checksum "$(shasum -a 256 "$archive" | awk '{print $1}')" \
  '{schema: "weather-routing-target-result-v1",
    target: "macos-arm64", repository_commit: $commit,
    plugin_version: $version, operating_system: "macOS",
    operating_system_version: $os, architecture: "arm64",
    compiler: $compiler, cmake_version: $cmake, wxwidgets_version: $wx,
    build_status: "passed", test_status: "passed",
    package_status: "passed", metadata_validation_status: "passed",
    stock_api_status: "passed", package_filename: $package,
    package_checksum_sha256: $checksum}' >"$artifact/result.json"
