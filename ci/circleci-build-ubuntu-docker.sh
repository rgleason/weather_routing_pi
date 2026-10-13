#!/usr/bin/env bash
set -euo pipefail
set -x

cd "${HOME}/project"
git submodule update --init --recursive

test -n "${DOCKER_IMAGE:-}"
test -n "${OCPN_TARGET:-}"

mkdir -p build artifacts .circleci-cache/linux-stock-host
# Retain diagnostics even when the build or installed-host gate fails.
retain_build_artifacts() {
  sudo chmod -R a+rw build
  sudo chmod -R a+rwX .circleci-cache/linux-stock-host
  if [[ -d build/artifacts ]]; then
    cp -a build/artifacts/. artifacts/
  fi
}
trap retain_build_artifacts EXIT
docker build --build-arg "BASE_IMAGE=${DOCKER_IMAGE}" \
  -f "${DOCKERFILE:-ci/Dockerfile.linux}" \
  -t weather-routing-linux-build ci
docker run --rm \
  -e "CIRCLE_PROJECT_REPONAME=${CIRCLE_PROJECT_REPONAME:-}" \
  -e "WEATHER_ROUTING_CI_XWEATHER=${WEATHER_ROUTING_CI_XWEATHER:-false}" \
  -e "WEATHER_ROUTING_API_HEADER_VERSION=${WEATHER_ROUTING_API_HEADER_VERSION:-1.21}" \
  -e "OCPN_TARGET=${OCPN_TARGET}" \
  -e "CMAKE_BUILD_PARALLEL_LEVEL=${CMAKE_BUILD_PARALLEL_LEVEL:-3}" \
  -e CCACHE_DIR=/stock-host-ccache \
  -v "${PWD}:/src:rw" \
  -v "${PWD}/build:/work" \
  -v "${PWD}/.circleci-cache/linux-stock-host:/stock-host-ccache:rw" \
  weather-routing-linux-build \
  /src/ci/build-linux-catalogue.sh
