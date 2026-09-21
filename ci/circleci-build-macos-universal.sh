#!/usr/bin/env bash
set -euo pipefail
set -x

# Load local environment if present
if [ -f ~/.config/local-build.rc ]; then source ~/.config/local-build.rc; fi

git submodule update --init

# Restore cached /usr/local if present
if [[ -n "$CI" && -f /tmp/local.cache.tar ]]; then
  sudo rm -rf /usr/local/*
  sudo tar -C /usr -xf /tmp/local.cache.tar
fi

rm -rf build && mkdir build
exec > >(tee build/build.log) 2>&1

export MACOSX_DEPLOYMENT_TARGET=10.10
export OPENSSL_ROOT_DIR='/usr/local'

# Ensure brew deps
brew list --versions libexif || brew update-reset

here=$(cd "$(dirname "$0")"; pwd)
while read -r pkg; do
  [[ -z "$pkg" || "$pkg" =~ ^# ]] && continue
  brew list --versions "$pkg" || brew install "$pkg" || :
  brew link --overwrite "$pkg" || :
done < "$here/../build-deps/macos-deps"

/usr/bin/python3 -m venv "$HOME/cs-venv"

# Install prebuilt wxWidgets universal dependencies
wget -q https://dl.cloudsmith.io/public/nohal/opencpn-plugins/raw/files/macos_deps_universal.tar.xz \
     -O /tmp/macos_deps_universal.tar.xz
sudo tar -C /usr/local -xJf /tmp/macos_deps_universal.tar.xz

cd build

cmake \
  -DCMAKE_INSTALL_PREFIX= \
  -DCMAKE_OSX_DEPLOYMENT_TARGET="${MACOSX_DEPLOYMENT_TARGET}" \
  -DOCPN_TARGET_TUPLE="darwin-wx32;10;universal" \
  -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" \
  -DwxWidgets_CONFIG_EXECUTABLE=/usr/local/bin/wx-config \
  -DwxWidgets_LIB_DIR=/usr/local/lib \
  -DCPACK_GENERATOR=TGZ \
  -DCPACK_PROJECT_CONFIG_FILE=../cmake/PluginCPackOptions.cmake \
  -DCPACK_PACKAGE_FILE_NAME="weather_routing_pi-${PLUGIN_VERSION}-darwin-wx32" \
  ..

echo "Building macOS universal plugin..."
cmake --build . --config Release --target install

echo "Running CPack..."
cpack -G TGZ

# Cache /usr/local for next CI run
if [ -n "$CI" ]; then
  tar -C /usr -cf /tmp/local.cache.tar local
fi
