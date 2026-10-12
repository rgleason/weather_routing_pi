#!/bin/sh
set -eu

export DEBIAN_FRONTEND=noninteractive
apt-get update
apt-get install -y --no-install-recommends \
  binutils \
  build-essential \
  ca-certificates \
  cmake \
  gettext \
  git \
  lsb-release \
  ninja-build \
  pkg-config \
  python3 \
  dbus-x11 \
  xvfb \
  tzdata \
  libbz2-dev \
  libcurl4-openssl-dev \
  libgl1-mesa-dev \
  libglu1-mesa-dev \
  libgtk-3-dev \
  libarchive-dev \
  libdrm-dev \
  libelf-dev \
  libexif-dev \
  libgdk-pixbuf-2.0-dev \
  libglew-dev \
  liblz4-dev \
  liblzma-dev \
  libpango1.0-dev \
  libshp-dev \
  libsqlite3-dev \
  libssl-dev \
  libtinyxml-dev \
  libudev-dev \
  libusb-1.0-0-dev \
  rapidjson-dev \
  libwxgtk3.2-dev \
  zlib1g-dev

rm -rf /var/lib/apt/lists/*
