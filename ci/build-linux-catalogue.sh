#!/usr/bin/env bash
set -euo pipefail

source_dir=${1:-/src}
work_dir=${2:-/work}
test_build=${work_dir}/test-build
package_build=${work_dir}/package-build
stage_dir=${work_dir}/stage
artifact_dir=${work_dir}/artifacts/${OCPN_TARGET:-linux}
log_dir=${artifact_dir}/logs
test_dir=${artifact_dir}/tests
package_dir=${artifact_dir}/package

mkdir -p "$test_build" "$package_build" "$stage_dir" \
  "$log_dir" "$test_dir" "$package_dir"

cmake -S "$source_dir" -B "$test_build" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/usr/local \
  -DWEATHER_ROUTING_STANDALONE_API=ON \
  -DOCPN_BUILD_TEST=ON 2>&1 | tee "$log_dir/configure-tests.log"
cmake --build "$test_build" \
  --parallel "${CMAKE_BUILD_PARALLEL_LEVEL:-2}" \
  2>&1 | tee "$log_dir/build-tests.log"
ctest --test-dir "$test_build" --output-on-failure \
  --output-junit "$test_dir/ctest.xml" \
  2>&1 | tee "$log_dir/test.log"

# Package from a separate tests-disabled tree. Older plugin packaging helpers
# may otherwise collect GoogleTest shared libraries from the build directory.
cmake -S "$source_dir" -B "$package_build" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/usr/local \
  -DWEATHER_ROUTING_STANDALONE_API=ON \
  -DOCPN_BUILD_TEST=OFF 2>&1 | tee "$log_dir/configure-package.log"
cmake --build "$package_build" \
  --parallel "${CMAKE_BUILD_PARALLEL_LEVEL:-2}" \
  2>&1 | tee "$log_dir/build-package.log"
DESTDIR="$stage_dir" cmake --install "$package_build" --prefix /usr \
  >"$log_dir/install.log" 2>&1
cmake --build "$package_build" --target package \
  2>&1 | tee "$log_dir/package.log"

"$source_dir/ci/test-catalogue-archive.sh" "$package_build" "$stage_dir" \
  2>&1 | tee "$log_dir/archive-validation.log"

find "$package_build" -maxdepth 1 -type f \
  \( -name '*.tar.gz' -o -name '*.xml' \) \
  -exec cp -f '{}' "$package_dir/" \;
sha256sum "$package_dir"/* >"$package_dir/SHA256SUMS"
