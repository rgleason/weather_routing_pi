#!/bin/sh
set -eu

export DEBIAN_FRONTEND=noninteractive

# Retry downloads up to 3 times to handle transient PPA 503 errors
echo 'Acquire::Retries "3";' > /etc/apt/apt.conf.d/80-retries

set -- \
  binutils \
  build-essential \
  ca-certificates \
  ccache \
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

apt-get update

# Ubuntu's HTTP security mirror can return 404 for URL-encoded package names
# which are available over HTTPS. Bootstrap the trust store in the base image.
ubuntu_sources=/etc/apt/sources.list.d/ubuntu.sources
if [ -f "$ubuntu_sources" ] &&
  grep -Eq '^URIs: http://(archive|security)\.ubuntu\.com/ubuntu/?$' "$ubuntu_sources"
then
  apt-get install -y --no-install-recommends ca-certificates
  sed -i -E 's@^URIs: http://((archive|security)\.ubuntu\.com/ubuntu/?$)@URIs: https://\1@' "$ubuntu_sources"
  apt-get update
fi

if ! apt-get install -y --no-install-recommends "$@"; then
  # A mirror can replace a package between the index refresh and the download.
  # Refresh the indexes before retrying so apt can select the current version.
  apt-get -o Acquire::http::No-Cache=true -o Acquire::https::No-Cache=true update
  apt-get install -y --no-install-recommends "$@"
fi

rm -rf /var/lib/apt/lists/*
