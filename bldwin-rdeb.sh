#!/bin/bash

# FE2 Testplugin
# REKWITHDEBINFO VERSION
# Use "./build-win.sh" to run cmake.
# Adjust this command for your setup and Plugin.
# Requires wxWidgets setup
# - /home/fcgle/source/ocpn-wxWidgets
# - /home/fcgle/source/ where all the plugins and OpenCPN repos are kept.
# --------------------------------------
# For Opencpn using MS Visual Studio 2022
# --------------------------------------
# Used for local builds and testing.
# Create an empty "[plugin]/build" directory
# Use Bash Prompt from the [plugin] root directory: "bash ./bldwin-rdeb.sh"
# Find any errors in the build/output.txt file
# Then use bash prompt to run cloudsmith-upload.sh command: "bash ./bldwin-rdeb.sh"
# Which adds the metadata file to the tarball gz file.
# Set local environment to find and use wxWidgets

# Enable command tracing

set -x 

# ------------------------------------------------------------
# wxWidgets settings (MSVC build)
# ------------------------------------------------------------
export wxDIR="C:/Users/fcgle/source/ocpn_wxWidgets"
export wxWIN="C:/Users/fcgle/source/ocpn_wxWidgets"
export wxWidgets_ROOT_DIR="C:/Users/fcgle/source/ocpn_wxWidgets"
export wxWidgets_LIB_DIR="C:/Users/fcgle/source/ocpn_wxWidgets/lib/vc_dll"
export wxWidgets_INCLUDE_DIR="C:/Users/fcgle/source/ocpn_wxWidgets/include"

export VCver=17
export VCstr="Visual Studio 17"

# ------------------------------------------------------------
# Clean build directory
# ------------------------------------------------------------
if [ -d "build" ]; then
    echo "Removing entire build directory."
    rm -rf build
fi

mkdir build
cd build

# ------------------------------------------------------------
# Configure with CMake (MSVC 2022, Win32, v143 toolset)
# ------------------------------------------------------------
cmake -T v143 -A Win32 \
  -DwxWidgets_ROOT_DIR="${wxWidgets_ROOT_DIR}" \
  -DwxWidgets_LIB_DIR="${wxWidgets_LIB_DIR}" \
  -DwxWidgets_INCLUDE_DIR="${wxWidgets_INCLUDE_DIR}" \
  -DOCPN_TARGET=MSVC \
  ..
 
# ------------------------------------------------------------
# Build and package
# ------------------------------------------------------------
cmake --build . --target package --config RelWithDebInfo > output.txt

# ------------------------------------------------------------
# Upload to Cloudsmith
# ------------------------------------------------------------
bash ./cloudsmith-upload.sh

# ------------------------------------------------------------
# Copy plugin DLL + PDB to OpenCPN dev tree
# ------------------------------------------------------------
cp -uv ./RelWithDebInfo/*_pi.dll C:/Users/fcgle/source/opencpn/build/RelWithDebInfo/plugins/
cp -uv ./RelWithDebInfo/*_pi.pdb C:/Users/fcgle/source/opencpn/build/RelWithDebInfo/plugins/ 

# Bash script completes tarball prep adding metadata into it.
# Find ${bold}"build/output.txt"${normal} file if the build is not successful.
# Other examples below.

# cp -rv ./SourceFolder ./DestFolder
# cp -r ./dist/* ./out
#    -r - Copy all files and folders inside a directory
#    -i - Ask before replacing files
#    -u - Copy only if the source is newer
#    -v - Verbose mode, show files being copied
# copy ..\build\relwithdebinfo\weather_routing_pi.dll to  C:\Users\fcgle\source\opencpn\build\RelWithDebInfo\plugins
# copy ..\build\relwithdebinfo\weather_routing_pi.pdb to  C:\Users\fcgle\source\opencpn\build\RelWithDebInfo\plugins

# cp -uv ./RelWithDebInfo/*_pi.dll C:/Users/fcgle/source/opencpn/build/RelWithDebInfo/plugins
# cp -uv ./RelWithDebInfo/*_pi.pdb C:/Users/fcgle/source/opencpn/build/RelWithDebInfo/plugins

#cp -uv ${CMAKE_CURRENT_BINARY_DIR}/RelWithDebInfo/*_pi.dll \
#      C:/Users/fcgle/source/opencpn/build/RelWithDebInfo/plugins/

#cp -uv ${CMAKE_CURRENT_BINARY_DIR}/RelWithDebInfo/*_pi.pdb \
      C:/Users/fcgle/source/opencpn/build/RelWithDebInfo/plugins/
