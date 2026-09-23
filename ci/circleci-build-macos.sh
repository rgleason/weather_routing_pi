#!/usr/bin/env bash

#
# Build the MacOS artifacts (universal, 10.15+, wx 3.1.5/3.2.1)
#

set -xe
set -o pipefail
export WX_NO_HOME=/usr/local


# ------------------------------------------------------------
#  Base environment
# ------------------------------------------------------------
export MACOSX_DEPLOYMENT_TARGET=10.15

# Check if the cache is with us. If not, re-install brew.
brew list --versions libexif || brew update-reset

for pkg in cairo cmake gettext libarchive libexif python3 wget; do
    brew list --versions "$pkg" || brew install "$pkg" || brew install "$pkg" || :
    brew link --overwrite "$pkg" || brew install "$pkg"
done

# ------------------------------------------------------------
#  Select wxWidgets bundle (10.15 builds)
# ------------------------------------------------------------
if [ -n "${WX_VER}" ] && [ "${WX_VER}" -eq "32" ]; then
    echo "Building for WXVERSION 32"
    WX_URL=https://download.opencpn.org/s/Djqm4SXzYjF8nBw/download
    WX_PREFIX=/tmp/wx321_opencpn50_macos1015
else
    echo "Building for WXVERSION 315"
    WX_URL=https://download.opencpn.org/s/MCiRiq4fJcKD56r/download
    WX_PREFIX=/tmp/wx315_opencpn50_macos1015
fi

WX_DOWNLOAD="${WX_PREFIX}.tar.xz"
WX_EXECUTABLE="${WX_PREFIX}/bin/wx-config"
WX_CONFIG="--prefix=${WX_PREFIX}"

# ------------------------------------------------------------
#  Clean stale cached wx bundles
# ------------------------------------------------------------
rm -rf /tmp/wx321_opencpn50_macos1010 \
       /tmp/wx315_opencpn50_macos1010 \
       /tmp/wx321_opencpn50_macos1015 \
       /tmp/wx315_opencpn50_macos1015

# ------------------------------------------------------------
#  Python virtual environment (for i18n, etc.)
# ------------------------------------------------------------
/usr/bin/python3 -m venv "$HOME/cs-venv"

# ------------------------------------------------------------
#  Download wx bundle (10.15) if needed
# ------------------------------------------------------------
if [ ! -f "$WX_DOWNLOAD" ]; then
    echo "Downloading $WX_DOWNLOAD"
    SERVER_RESPONSE=$(wget --server-response -O "$WX_DOWNLOAD" "$WX_URL" 2>&1 | grep "HTTP/" | awk '{print $2}')
    if [ "$SERVER_RESPONSE" -ne 200 ]; then
        echo "Fatal error: could not download $WX_DOWNLOAD. Server response: $SERVER_RESPONSE."
        exit 1
    fi
fi

if [ -f "$WX_DOWNLOAD" ]; then
    echo "$WX_DOWNLOAD exists"
else
    echo "Fatal error: $WX_DOWNLOAD does not exist"
    exit 1
fi

# ------------------------------------------------------------
#  Unpack wx bundle
# ------------------------------------------------------------
tar xJf "$WX_DOWNLOAD" -C /tmp

# ------------------------------------------------------------
#  Force our wx tarball to be first in PATH
# ------------------------------------------------------------
export PATH="${WX_PREFIX}/bin:${PATH}"

# ------------------------------------------------------------
#  Ensure gettext in PATH
# ------------------------------------------------------------
INCLUDE_DIR_GETTEXT="/usr/local/opt/gettext/bin:"

if [[ ":$PATH:" != *"$INCLUDE_DIR_GETTEXT"* ]]; then
    echo "Your path is missing $INCLUDE_DIR_GETTEXT. Trying to add it automatically:"
    export PATH="${INCLUDE_DIR_GETTEXT}${PATH}"
    echo "export PATH=\"${INCLUDE_DIR_GETTEXT}\$PATH\"" >> "$HOME/.bash_profile"
else
    echo "Path includes $INCLUDE_DIR_GETTEXT"
fi

# ------------------------------------------------------------
#  Force use of downloaded wx-config, not Homebrew wx
# ------------------------------------------------------------
export WX_CONFIG_EXECUTABLE="$WX_EXECUTABLE"
export wxWidgets_CONFIG_EXECUTABLE="$WX_EXECUTABLE"
export wxWidgets_CONFIG_OPTIONS="$WX_CONFIG"

# Put our wx bin first in PATH so CMake finds the right wx-config
export PATH="${WX_PREFIX}/bin:${PATH}"

# ------------------------------------------------------------
#  Update submodules
# ------------------------------------------------------------
git submodule update --init opencpn-libs

# ------------------------------------------------------------
#  Configure + build + install + package
# ------------------------------------------------------------
rm -rf build && mkdir build && cd build

cmake \
  -DwxWidgets_CONFIG_EXECUTABLE="$WX_EXECUTABLE" \
  -DwxWidgets_CONFIG_OPTIONS="$WX_CONFIG" \
  -DCMAKE_INSTALL_PREFIX=app/files \
  -DBUILD_TYPE_PACKAGE:STRING=tarball \
  -DCMAKE_OSX_DEPLOYMENT_TARGET="$MACOSX_DEPLOYMENT_TARGET" \
  -DCMAKE_OSX_ARCHITECTURES="x86_64;arm64" \
  ..


if [ $? -ne 0 ]; then
    echo "ERROR: cmake configure failed."
    exit 1
fi

make
if [ $? -ne 0 ]; then
    echo "ERROR: make failed — no dylib produced."
    exit 1
fi

make install
if [ $? -ne 0 ]; then
    echo "ERROR: make install failed."
    exit 1
fi

make package
if [ $? -ne 0 ]; then
    echo "ERROR: make package failed."
    exit 1
fi

# Optional: only run otool if a dylib exists
if ls *.dylib 1>/dev/null 2>&1; then
    echo "Inspecting dylib with otool-classic"
    /Applications/Xcode-15.4.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/otool-classic -L *.dylib || true
else
    echo "WARNING: No dylib found in build/ — macOS build may have failed before linking."
fi
