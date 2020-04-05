#!/bin/sh  -xe

#
# Build the mingw artifacts inside the Fedora container
#

set -xe

su -c "dnf install -q -y sudo dnf-plugins-core"
sudo dnf builddep -y mingw/fedora/opencpn-deps.spec
cd buildwin
wget https://downloads.sourceforge.net/project/opencpnplugins/opencpn_packaging_data/PVW32Con.exe
cd ..
rm -rf build; mkdir build; cd build
cmake -DCMAKE_TOOLCHAIN_FILE=../mingw/fedora/toolchain.cmake ..
make -j2
make package
