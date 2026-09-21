#!/usr/bin/env bash
set -euo pipefail

WX_VERSION=3.2.4
PREFIX=/usr/local
SDKROOT=$(xcrun --sdk macosx --show-sdk-path)

JOBS=${JOBS:-$(sysctl -n hw.ncpu)}

mkdir -p ~/wx-src && cd ~/wx-src

if [ ! -d "wxWidgets-${WX_VERSION}" ]; then
  curl -L "https://github.com/wxWidgets/wxWidgets/releases/download/v${WX_VERSION}/wxWidgets-${WX_VERSION}.tar.bz2" \
    -o "wxWidgets-${WX_VERSION}.tar.bz2"
  tar xjf "wxWidgets-${WX_VERSION}.tar.bz2"
fi

cd "wxWidgets-${WX_VERSION}"

export MACOSX_DEPLOYMENT_TARGET=10.10
export SDKROOT
export CFLAGS="-isysroot ${SDKROOT}"
export CXXFLAGS="-isysroot ${SDKROOT}"
export LDFLAGS="-isysroot ${SDKROOT}"

./configure \
  --prefix="${PREFIX}" \
  --enable-universal_binary=arm64,x86_64 \
  --with-osx_cocoa \
  --enable-std_string \
  --enable-std_iostreams \
  --enable-unicode \
  --disable-mediactrl \
  --disable-webview \
  --disable-webviewwebkit \
  --disable-webviewchromium \
  --disable-webviewedge \
  --disable-debug \
  --enable-optimise

make -j"${JOBS}"
make install

wx-config --version=3.2
wx-config --cxxflags
wx-config --libs std,aui,gl
